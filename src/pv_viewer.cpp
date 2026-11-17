#include "pv_viewer.h"
#include "pv_registry.h"

namespace pv {

bool ViewerWindow::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    // CS_DBLCLKS 是收到 WM_LBUTTONDBLCLK 的前提。
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &ViewerWindow::WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    return ::RegisterClassExW(&wc) != 0;
}

HWND ViewerWindow::Create(HINSTANCE instance, const std::wstring& initialFile, int showCommand) {
    auto* window = new ViewerWindow();
    window->initialFile_ = initialFile;

    // 无边框窗口居中于主显示器工作区。
    RECT work{};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfo(::MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        work = monitor.rcWork;
    } else {
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    const int width = 1000;
    const int height = 740;
    const int x = work.left + (work.right - work.left - width) / 2;
    const int y = work.top + (work.bottom - work.top - height) / 2;

    HWND hwnd = ::CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
        kClassName,
        L"Picture Viewer",
        WS_POPUP,
        x, y, width, height,
        nullptr, nullptr, instance, window);

    if (!hwnd) {
        delete window;
        return nullptr;
    }

    ::ShowWindow(hwnd, showCommand);
    ::UpdateWindow(hwnd);
    if (!initialFile.empty()) {
        window->OpenFile(initialFile);
    }
    return hwnd;
}

LRESULT CALLBACK ViewerWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ViewerWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ViewerWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->hwnd_ = hwnd;
        }
    } else {
        self = reinterpret_cast<ViewerWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(message, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT ViewerWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_NCHITTEST:
        return OnNcHitTest(lParam);

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = { 480, 320 };
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ::ScreenToClient(hwnd_, &pt);
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), pt);
        return 0;
    }

    case WM_LBUTTONDOWN:
        OnLButtonDown({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp();
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLButtonDblClk({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_RBUTTONUP:
        OnRButtonUp({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_MOUSELEAVE:
        mouseTracking_ = false;
        if (hoveredButton_ != BarButton::None) {
            hoveredButton_ = BarButton::None;
            Invalidate();
        }
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;

    case WM_TIMER:
        if (wParam == kAnimationTimerId) {
            OnTimer();
        }
        return 0;

    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_SETCURSOR: {
        // WM_SETCURSOR 的 lParam 不是坐标，需要查询实际鼠标位置。
        POINT cursor{};
        ::GetCursorPos(&cursor);
        ::ScreenToClient(hwnd_, &cursor);
        if (OnSetCursor(cursor)) {
            return TRUE;
        }
        return ::DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    default:
        return ::DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
// 窗口事件
// ---------------------------------------------------------------------------
void ViewerWindow::OnCreate() {
    ::DragAcceptFiles(hwnd_, TRUE);
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    OnSize(client.right, client.bottom);
    UpdateInfoText();
}

void ViewerWindow::OnDestroy() {
    if (animating_ && hwnd_) {
        ::KillTimer(hwnd_, kAnimationTimerId);
        animating_ = false;
    }
    ::DragAcceptFiles(hwnd_, FALSE);
    ::PostQuitMessage(0);
    delete this;
}

void ViewerWindow::OnSize(int width, int height) {
    clientWidth_ = width;
    clientHeight_ = height;
    RecomputeBarLayout(width);

    ScopedWindowDc screenDc(hwnd_);
    backBuffer_ = std::make_unique<BackBuffer>(screenDc.get(), width, height);

    if (document_) {
        if (fitMode_) {
            FitToWindow(false);
        } else {
            double zoom = targetView_.zoom;
            double x = targetView_.offsetX;
            double y = targetView_.offsetY;
            ClampView(zoom, x, y);
            view_ = targetView_ = { zoom, x, y };
            Invalidate();
        }
    }
}

void ViewerWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = ::BeginPaint(hwnd_, &ps);

    RECT client{};
    ::GetClientRect(hwnd_, &client);

    const bool showBar = !fullscreen_;
    if (backBuffer_ && backBuffer_->Dc()) {
        Renderer::Draw(backBuffer_->Dc(), client, document_.get(), view_,
                       infoText_, buttons_, hoveredButton_,
                       showBar, maximized_, fullscreen_);
        backBuffer_->Present(hdc, clientWidth_, clientHeight_);
    } else {
        Renderer::Draw(hdc, client, document_.get(), view_,
                       infoText_, buttons_, hoveredButton_,
                       showBar, maximized_, fullscreen_);
    }

    ::EndPaint(hwnd_, &ps);
}

LRESULT ViewerWindow::OnNcHitTest(LPARAM lParam) {
    if (maximized_ || fullscreen_) {
        return HTCLIENT;
    }

    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ::ScreenToClient(hwnd_, &pt);
    RECT client{};
    ::GetClientRect(hwnd_, &client);

    const int m = kResizeBorder;
    const bool left = pt.x < m;
    const bool right = pt.x >= client.right - m;
    const bool top = pt.y < m;
    const bool bottom = pt.y >= client.bottom - m;

    if (left && top)    return HTTOPLEFT;
    if (right && top)   return HTTOPRIGHT;
    if (left && bottom) return HTBOTTOMLEFT;
    if (right && bottom) return HTBOTTOMRIGHT;
    if (left)           return HTLEFT;
    if (right)          return HTRIGHT;
    if (top)            return HTTOP;
    if (bottom)         return HTBOTTOM;
    return HTCLIENT;
}

// ---------------------------------------------------------------------------
// 鼠标交互
// ---------------------------------------------------------------------------
void ViewerWindow::OnMouseWheel(int delta, POINT pt) {
    if (!document_) {
        return;
    }

    const double factor = std::pow(1.0015, static_cast<double>(delta));
    double zoom = ClampD(targetView_.zoom * factor, kMinZoom, kMaxZoom);

    // 以鼠标指针为锚点：缩放前后指针下的图像坐标保持不变。
    const double imageX = (pt.x - targetView_.offsetX) / targetView_.zoom;
    const double imageY = (pt.y - targetView_.offsetY) / targetView_.zoom;
    double offsetX = pt.x - imageX * zoom;
    double offsetY = pt.y - imageY * zoom;

    ClampView(zoom, offsetX, offsetY);
    fitMode_ = false;
    StartAnimation(zoom, offsetX, offsetY);
}

void ViewerWindow::OnLButtonDown(POINT pt) {
    if (!fullscreen_ && pt.y < (std::min)(Renderer::kTopBarHeight, clientHeight_)) {
        const BarButton button = HitTestBar(pt);
        switch (button) {
        case BarButton::Prev:       Advance(-1); return;
        case BarButton::Next:       Advance(+1); return;
        case BarButton::Fit:        FitToWindow(true); return;
        case BarButton::Minimize:   ::ShowWindow(hwnd_, SW_MINIMIZE); return;
        case BarButton::Maximize:   ToggleMaximize(); return;
        case BarButton::Fullscreen: ToggleFullscreen(); return;
        case BarButton::Close:      ::PostMessageW(hwnd_, WM_CLOSE, 0, 0); return;
        default:
            break;
        }

        // 点击信息栏空白处：拖动无边框窗口（最大化状态下不移动）。
        if (!maximized_) {
            POINT screen = pt;
            ::ClientToScreen(hwnd_, &screen);
            RECT window{};
            ::GetWindowRect(hwnd_, &window);
            windowDragStart_ = screen;
            windowPosStart_ = { window.left, window.top };
            draggingWindow_ = true;
            ::SetCapture(hwnd_);
        }
        return;
    }

    const RECT content = ContentRect();
    if (!document_ || !::PtInRect(&content, pt)) {
        return;
    }

    dragging_ = true;
    dragMoved_ = false;
    dragStart_ = pt;
    dragOffsetStartX_ = view_.offsetX;
    dragOffsetStartY_ = view_.offsetY;
    ::SetCapture(hwnd_);
}

void ViewerWindow::OnMouseMove(POINT pt) {
    if (!mouseTracking_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        mouseTracking_ = ::TrackMouseEvent(&track) != FALSE;
    }

    if (draggingWindow_) {
        POINT screen = pt;
        ::ClientToScreen(hwnd_, &screen);
        const int dx = screen.x - windowDragStart_.x;
        const int dy = screen.y - windowDragStart_.y;
        ::SetWindowPos(hwnd_, nullptr,
                       windowPosStart_.x + dx, windowPosStart_.y + dy,
                       0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    const BarButton hovered = HitTestBar(pt);
    if (hovered != hoveredButton_) {
        hoveredButton_ = hovered;
        Invalidate();
    }

    if (!dragging_) {
        return;
    }

    const int dx = pt.x - dragStart_.x;
    const int dy = pt.y - dragStart_.y;
    if (!dragMoved_ && (std::abs(dx) > 2 || std::abs(dy) > 2)) {
        dragMoved_ = true;
        StopAnimation();
    }
    if (!dragMoved_) {
        return;
    }

    double zoom = view_.zoom;
    double offsetX = dragOffsetStartX_ + dx;
    double offsetY = dragOffsetStartY_ + dy;
    ClampView(zoom, offsetX, offsetY);
    view_ = targetView_ = { zoom, offsetX, offsetY };
    Invalidate();
}

void ViewerWindow::OnLButtonUp() {
    if (draggingWindow_) {
        draggingWindow_ = false;
        ::ReleaseCapture();
    }
    if (dragging_) {
        dragging_ = false;
        dragMoved_ = false;
        ::ReleaseCapture();
    }
}

void ViewerWindow::OnLButtonDblClk(POINT pt) {
    const RECT content = ContentRect();
    if (document_ && ::PtInRect(&content, pt)) {
        ToggleActualSize();
        return;
    }
    if (!fullscreen_ && pt.y < (std::min)(Renderer::kTopBarHeight, clientHeight_) &&
        HitTestBar(pt) == BarButton::None) {
        ToggleMaximize();
    }
}

void ViewerWindow::OnRButtonUp(POINT pt) {
    const RECT content = ContentRect();
    if (::PtInRect(&content, pt)) {
        Advance(+1);
    }
}

void ViewerWindow::OnKeyDown(WPARAM key) {
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;

    switch (key) {
    case VK_ESCAPE:
        if (fullscreen_) {
            ToggleFullscreen();
        } else {
            ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        break;

    case VK_F11:
        ToggleFullscreen();
        break;

    case VK_LEFT:
        Advance(-1);
        break;

    case VK_RIGHT:
        Advance(+1);
        break;

    case VK_HOME:
        if (images_) images_->Select(0), Advance(0);
        break;

    case VK_END:
        if (images_ && images_->Size() > 0) images_->Select(images_->Size() - 1), Advance(0);
        break;

    case 'F':
    case 'f':
        FitToWindow(true);
        break;

    case '1': {
        if (!document_) break;
        RECT c = ContentRect();
        double zoom = 1.0;
        double x = (c.left + c.right) / 2.0 - document_->width / 2.0;
        double y = (c.top + c.bottom) / 2.0 - document_->height / 2.0;
        ClampView(zoom, x, y);
        fitMode_ = false;
        StartAnimation(zoom, x, y);
        break;
    }

    case VK_ADD:
    case '=':
        ZoomAt({ clientWidth_ / 2, clientHeight_ / 2 }, 1.25);
        break;

    case VK_SUBTRACT:
    case '-':
        ZoomAt({ clientWidth_ / 2, clientHeight_ / 2 }, 0.8);
        break;

    case 'A':
    case 'a':
        if (ctrl) {
            const bool ok = RegisterAssociations();
            ::MessageBoxW(hwnd_,
                          ok ? L"已将常见图片格式关联到本程序（当前用户）。"
                             : L"关联失败，请检查权限。",
                          L"文件关联", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        }
        break;

    case 'D':
    case 'd':
        if (ctrl) {
            const bool ok = UnregisterAssociations();
            ::MessageBoxW(hwnd_,
                          ok ? L"已取消本程序的文件关联。"
                             : L"取消关联失败。",
                          L"文件关联", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        }
        break;

    default:
        break;
    }
}

void ViewerWindow::OnTimer() {
    const double k = kAnimationStep;
    view_.zoom += (targetView_.zoom - view_.zoom) * k;
    view_.offsetX += (targetView_.offsetX - view_.offsetX) * k;
    view_.offsetY += (targetView_.offsetY - view_.offsetY) * k;

    const bool done = std::abs(targetView_.zoom - view_.zoom) < 0.0005 &&
                      std::abs(targetView_.offsetX - view_.offsetX) < 0.25 &&
                      std::abs(targetView_.offsetY - view_.offsetY) < 0.25;
    if (done) {
        view_ = targetView_;
        StopAnimation();
    }
    UpdateInfoText();
    Invalidate();
}

void ViewerWindow::OnDropFiles(HDROP drop) {
    UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count == 0) {
        return;
    }
    std::wstring path(MAX_PATH, L'\0');
    UINT length = ::DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size()));
    path.resize(length);
    ::DragFinish(drop);
    if (!path.empty()) {
        OpenFile(path);
    }
}

bool ViewerWindow::OnSetCursor(POINT pt) {
    if (!fullscreen_ && pt.y < (std::min)(Renderer::kTopBarHeight, clientHeight_) &&
        HitTestBar(pt) != BarButton::None) {
        ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
        return true;
    }
    const RECT content = ContentRect();
    if (document_ && ::PtInRect(&content, pt) && view_.zoom > 1.0) {
        ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 视图计算
// ---------------------------------------------------------------------------
RECT ViewerWindow::ContentRect() const {
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    if (!fullscreen_) {
        client.top += (std::min)(Renderer::kTopBarHeight, clientHeight_);
    }
    return client;
}

BarButton ViewerWindow::HitTestBar(POINT pt) const {
    if (fullscreen_ || pt.y < 0 || pt.y > (std::min)(Renderer::kTopBarHeight, clientHeight_)) {
        return BarButton::None;
    }
    if (::PtInRect(&buttons_.prev, pt))       return BarButton::Prev;
    if (::PtInRect(&buttons_.next, pt))       return BarButton::Next;
    if (::PtInRect(&buttons_.fit, pt))        return BarButton::Fit;
    if (::PtInRect(&buttons_.minimize, pt))   return BarButton::Minimize;
    if (::PtInRect(&buttons_.maximize, pt))   return BarButton::Maximize;
    if (::PtInRect(&buttons_.fullscreen, pt)) return BarButton::Fullscreen;
    if (::PtInRect(&buttons_.close, pt))      return BarButton::Close;
    return BarButton::None;
}

void ViewerWindow::RecomputeBarLayout(int clientWidth) {
    const int barHeight = Renderer::kTopBarHeight;
    const int buttonWidth = 34;

    int right = clientWidth;
    auto place = [&](RECT& target, int gapBefore) {
        right -= gapBefore;
        right -= buttonWidth;
        target = { right, 0, right + buttonWidth, barHeight };
    };

    place(buttons_.close, 0);
    place(buttons_.fullscreen, 0);
    place(buttons_.maximize, 0);
    place(buttons_.minimize, 0);
    place(buttons_.fit, 12);
    place(buttons_.next, 2);
    place(buttons_.prev, 2);
}

void ViewerWindow::UpdateInfoText() {
    if (!document_) {
        infoText_ = L"Picture Viewer    拖放图片到窗口以开始浏览";
        return;
    }
    const int percent = static_cast<int>(std::llround(view_.zoom * 100.0));
    infoText_ = std::wstring(L"Picture Viewer    ") +
                std::to_wstring(percent) + L"%    " +
                document_->fileName + L"    " +
                std::to_wstring(document_->width) + L"\u00D7" +
                std::to_wstring(document_->height) + L"    " +
                FormatBytes(document_->fileSize);
}

void ViewerWindow::ClampView(double& zoom, double& offsetX, double& offsetY) const {
    if (!document_) {
        return;
    }
    const RECT content = ContentRect();
    const double contentWidth = static_cast<double>(content.right - content.left);
    const double contentHeight = static_cast<double>(content.bottom - content.top);
    const double imageWidth = document_->width * zoom;
    const double imageHeight = document_->height * zoom;
    const double centerX = (content.left + content.right) / 2.0;
    const double centerY = (content.top + content.bottom) / 2.0;

    // 图片比视口小：保持居中；否则限制拖拽范围，避免把图片拖出视野。
    if (imageWidth <= contentWidth) {
        offsetX = centerX - imageWidth / 2.0;
    } else {
        offsetX = ClampD(offsetX, content.right - imageWidth, content.left);
    }

    if (imageHeight <= contentHeight) {
        offsetY = centerY - imageHeight / 2.0;
    } else {
        offsetY = ClampD(offsetY, content.bottom - imageHeight, content.top);
    }
}

void ViewerWindow::StartAnimation(double zoom, double offsetX, double offsetY) {
    targetView_ = { zoom, offsetX, offsetY };
    if (!animating_) {
        animating_ = true;
        ::SetTimer(hwnd_, kAnimationTimerId, 16, nullptr);
    }
    UpdateInfoText();
    Invalidate();
}

void ViewerWindow::StopAnimation() {
    if (animating_ && hwnd_) {
        ::KillTimer(hwnd_, kAnimationTimerId);
    }
    animating_ = false;
    targetView_ = view_;
}

void ViewerWindow::Invalidate() const {
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// 图片切换 / 适配
// ---------------------------------------------------------------------------
bool ViewerWindow::OpenFile(const std::wstring& path) {
    auto doc = ImageLoader::Load(path);
    if (!doc) {
        ::MessageBoxW(hwnd_, (L"无法打开图片：\n" + path).c_str(),
                      L"Picture Viewer", MB_OK | MB_ICONWARNING);
        return false;
    }

    auto list = std::make_unique<ImageList>();
    list->SetFile(path);

    StopAnimation();
    images_ = std::move(list);
    document_ = std::move(doc);
    hoveredButton_ = BarButton::None;
    FitToWindow(false);

    ::SetWindowTextW(hwnd_, (document_->fileName + L" - Picture Viewer").c_str());
    return true;
}

bool ViewerWindow::Advance(int direction) {
    if (!images_ || images_->Empty()) {
        return false;
    }

    const std::size_t total = images_->Size();
    for (std::size_t attempt = 0; attempt < total; ++attempt) {
        if (direction > 0) {
            images_->Next();
        } else if (direction < 0) {
            images_->Previous();
        }

        auto doc = ImageLoader::Load(images_->Current());
        if (!doc) {
            continue;
        }

        StopAnimation();
        document_ = std::move(doc);
        FitToWindow(false);
        ::SetWindowTextW(hwnd_, (document_->fileName + L" - Picture Viewer").c_str());
        return true;
    }
    return false;
}

void ViewerWindow::FitToWindow(bool animate) {
    if (!document_) {
        return;
    }
    const RECT content = ContentRect();
    double zoom = Renderer::FitZoom(
        *document_, content.right - content.left, content.bottom - content.top);
    const double centerX = (content.left + content.right) / 2.0;
    const double centerY = (content.top + content.bottom) / 2.0;
    double offsetX = centerX - document_->width * zoom / 2.0;
    double offsetY = centerY - document_->height * zoom / 2.0;
    ClampView(zoom, offsetX, offsetY);

    fitMode_ = true;
    if (animate) {
        StartAnimation(zoom, offsetX, offsetY);
    } else {
        StopAnimation();
        view_ = targetView_ = { zoom, offsetX, offsetY };
        UpdateInfoText();
        Invalidate();
    }
}

void ViewerWindow::ToggleActualSize() {
    if (fitMode_) {
        if (!document_) {
            return;
        }
        const RECT content = ContentRect();
        double zoom = 1.0;
        double offsetX = (content.left + content.right) / 2.0 - document_->width / 2.0;
        double offsetY = (content.top + content.bottom) / 2.0 - document_->height / 2.0;
        ClampView(zoom, offsetX, offsetY);
        fitMode_ = false;
        StartAnimation(zoom, offsetX, offsetY);
    } else {
        FitToWindow(true);
    }
}

void ViewerWindow::ZoomAt(POINT pt, double factor) {
    if (!document_) {
        return;
    }
    const RECT content = ContentRect();
    if (!::PtInRect(&content, pt)) {
        return;
    }
    double zoom = ClampD(targetView_.zoom * factor, kMinZoom, kMaxZoom);
    const double imageX = (pt.x - targetView_.offsetX) / targetView_.zoom;
    const double imageY = (pt.y - targetView_.offsetY) / targetView_.zoom;
    double offsetX = pt.x - imageX * zoom;
    double offsetY = pt.y - imageY * zoom;
    ClampView(zoom, offsetX, offsetY);

    fitMode_ = false;
    StartAnimation(zoom, offsetX, offsetY);
}

// ---------------------------------------------------------------------------
// 无边框窗口：最大化 / 全屏
// ---------------------------------------------------------------------------
RECT ViewerWindow::WorkAreaFor(HWND hwnd) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfo(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return monitor.rcWork;
    }
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

RECT ViewerWindow::MonitorAreaFor(HWND hwnd) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfo(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return monitor.rcMonitor;
    }
    return { 0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN) };
}

void ViewerWindow::ApplyPlacement(const RECT& rect, HWND insertAfter, UINT flags) {
    ::SetWindowPos(hwnd_, insertAfter,
                   rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top,
                   flags | SWP_NOACTIVATE);
}

void ViewerWindow::ToggleMaximize() {
    if (fullscreen_) {
        return;
    }
    if (maximized_) {
        ApplyPlacement(normalRect_, nullptr, SWP_NOZORDER);
        maximized_ = false;
    } else {
        ::GetWindowRect(hwnd_, &normalRect_);
        ApplyPlacement(WorkAreaFor(hwnd_), nullptr, SWP_NOZORDER);
        maximized_ = true;
    }
    Invalidate();
}

void ViewerWindow::ToggleFullscreen() {
    if (fullscreen_) {
        ApplyPlacement(fullscreenRestoreRect_, HWND_NOTOPMOST, SWP_NOZORDER);
        fullscreen_ = false;
        maximized_ = fullscreenWasMaximized_;
    } else {
        ::GetWindowRect(hwnd_, &fullscreenRestoreRect_);
        fullscreenWasMaximized_ = maximized_;
        maximized_ = false;
        ApplyPlacement(MonitorAreaFor(hwnd_), HWND_TOPMOST, SWP_NOZORDER);
        fullscreen_ = true;
        hoveredButton_ = BarButton::None;
    }
    Invalidate();
}

} // namespace pv
