#include "pv_render.h"

namespace pv {
namespace {

Gdiplus::Color ButtonColor(bool hovered, bool isClose) {
    if (!hovered) {
        return Gdiplus::Color(255, 44, 44, 48);
    }
    return isClose ? Gdiplus::Color(255, 210, 52, 60)
                   : Gdiplus::Color(255, 82, 82, 90);
}

void FillRect(Gdiplus::Graphics& g, const RECT& r, const Gdiplus::Color& color) {
    Gdiplus::SolidBrush brush(color);
    g.FillRectangle(&brush,
                    static_cast<float>(r.left), static_cast<float>(r.top),
                    static_cast<float>(r.right - r.left),
                    static_cast<float>(r.bottom - r.top));
}

Gdiplus::PointF Center(const RECT& r) {
    return Gdiplus::PointF(static_cast<float>(r.left + r.right) / 2.0f,
                           static_cast<float>(r.top + r.bottom) / 2.0f);
}

void DrawArrow(Gdiplus::Graphics& g, const RECT& r, bool left, bool hovered) {
    FillRect(g, r, ButtonColor(hovered, false));

    const Gdiplus::PointF c = Center(r);
    const float size = 5.0f;
    Gdiplus::PointF points[3];
    if (left) {
        points[0] = Gdiplus::PointF(c.X + size * 0.5f, c.Y - size);
        points[1] = Gdiplus::PointF(c.X + size * 0.5f, c.Y + size);
        points[2] = Gdiplus::PointF(c.X - size * 0.6f, c.Y);
    } else {
        points[0] = Gdiplus::PointF(c.X - size * 0.5f, c.Y - size);
        points[1] = Gdiplus::PointF(c.X - size * 0.5f, c.Y + size);
        points[2] = Gdiplus::PointF(c.X + size * 0.6f, c.Y);
    }
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 228, 228, 234));
    g.FillPolygon(&brush, points, 3);
}

void DrawFitIcon(Gdiplus::Graphics& g, const RECT& r, bool hovered) {
    FillRect(g, r, ButtonColor(hovered, false));

    const float l = static_cast<float>(r.left) + 8.0f;
    const float t = static_cast<float>(r.top) + 8.0f;
    const float rr = static_cast<float>(r.right) - 8.0f;
    const float b = static_cast<float>(r.bottom) - 8.0f;
    const float tick = 4.0f;

    Gdiplus::Pen pen(Gdiplus::Color(255, 228, 228, 234), 1.4f);
    g.DrawLine(&pen, l, t + tick, l, t);
    g.DrawLine(&pen, l, t, l + tick, t);
    g.DrawLine(&pen, rr - tick, t, rr, t);
    g.DrawLine(&pen, rr, t, rr, t + tick);
    g.DrawLine(&pen, l, b - tick, l, b);
    g.DrawLine(&pen, l, b, l + tick, b);
    g.DrawLine(&pen, rr - tick, b, rr, b);
    g.DrawLine(&pen, rr, b, rr, b - tick);
}

void DrawMinimizeIcon(Gdiplus::Graphics& g, const RECT& r, bool hovered) {
    FillRect(g, r, ButtonColor(hovered, false));
    const Gdiplus::PointF c = Center(r);
    Gdiplus::Pen pen(Gdiplus::Color(255, 228, 228, 234), 1.4f);
    g.DrawLine(&pen, c.X - 5.0f, c.Y + 2.5f, c.X + 5.0f, c.Y + 2.5f);
}

void DrawMaximizeIcon(Gdiplus::Graphics& g, const RECT& r, bool hovered, bool maximized) {
    FillRect(g, r, ButtonColor(hovered, false));
    const Gdiplus::PointF c = Center(r);
    Gdiplus::Pen pen(Gdiplus::Color(255, 228, 228, 234), 1.3f);

    if (!maximized) {
        g.DrawRectangle(&pen, c.X - 5.5f, c.Y - 5.0f, 11.0f, 10.0f);
        g.DrawLine(&pen, c.X - 5.5f, c.Y - 2.5f, c.X + 5.5f, c.Y - 2.5f);
    } else {
        g.DrawRectangle(&pen, c.X - 6.0f, c.Y - 3.0f, 9.0f, 8.0f);
        g.DrawRectangle(&pen, c.X - 2.0f, c.Y - 6.0f, 9.0f, 8.0f);
    }
}

void DrawPinIcon(Gdiplus::Graphics& g, const RECT& r, bool hovered, bool pinned) {
    FillRect(g, r, pinned ? Gdiplus::Color(255, 48, 104, 126)
                          : ButtonColor(hovered, false));
    const Gdiplus::PointF c = Center(r);
    const Gdiplus::Color color = pinned ? Gdiplus::Color(255, 130, 232, 236)
                                        : Gdiplus::Color(255, 228, 228, 234);
    Gdiplus::Pen pen(color, 1.4f);
    Gdiplus::SolidBrush brush(color);
    g.FillEllipse(&brush, c.X - 3.5f, c.Y - 6.0f, 7.0f, 7.0f);
    g.DrawLine(&pen, c.X - 1.0f, c.Y + 0.5f, c.X + 4.0f, c.Y + 6.5f);
    g.DrawLine(&pen, c.X + 1.0f, c.Y + 6.5f, c.X + 7.0f, c.Y + 6.5f);
}

void DrawFullscreenIcon(Gdiplus::Graphics& g, const RECT& r, bool hovered) {
    FillRect(g, r, ButtonColor(hovered, false));
    const float l = static_cast<float>(r.left) + 8.0f;
    const float t = static_cast<float>(r.top) + 8.0f;
    const float rr = static_cast<float>(r.right) - 8.0f;
    const float b = static_cast<float>(r.bottom) - 8.0f;

    Gdiplus::Pen pen(Gdiplus::Color(255, 228, 228, 234), 1.5f);
    const float arm = 5.0f;
    // 四个向外的对角箭头
    g.DrawLine(&pen, l, t + arm, l, t);
    g.DrawLine(&pen, l, t, l + arm, t);
    g.DrawLine(&pen, rr - arm, t, rr, t);
    g.DrawLine(&pen, rr, t, rr, t + arm);
    g.DrawLine(&pen, l, b - arm, l, b);
    g.DrawLine(&pen, l, b, l + arm, b);
    g.DrawLine(&pen, rr - arm, b, rr, b);
    g.DrawLine(&pen, rr, b, rr, b - arm);
    // 中心小方块，与「适应窗口」图标区分
    g.DrawRectangle(&pen, (l + rr) / 2.0f - 3.0f, (t + b) / 2.0f - 3.0f, 6.0f, 6.0f);
}

void DrawCloseIcon(Gdiplus::Graphics& g, const RECT& r, bool hovered) {
    FillRect(g, r, ButtonColor(hovered, true));
    const Gdiplus::PointF c = Center(r);
    Gdiplus::Pen pen(Gdiplus::Color(255, 242, 242, 246), 1.5f);
    g.DrawLine(&pen, c.X - 5.0f, c.Y - 5.0f, c.X + 5.0f, c.Y + 5.0f);
    g.DrawLine(&pen, c.X + 5.0f, c.Y - 5.0f, c.X - 5.0f, c.Y + 5.0f);
}

struct HelpLine {
    const wchar_t* text;
    int style; // 0=正文 1=小节标题 2=主标题 3=副标题
};

void DrawHelp(Gdiplus::Graphics& g, const RECT& content) {
    static const HelpLine kLines[] = {
        { L"Picture Viewer", 2 },
        { L"基于 Win32 API / GDI+ / WIC 的轻量级图片查看器", 3 },
        { L"", 0 },
        { L"打开图片", 1 },
        { L"  将图片拖放到窗口，或在命令行传入图片路径", 0 },
        { L"", 0 },
        { L"浏览与缩放", 1 },
        { L"  滚轮         以鼠标指针为中心平滑缩放", 0 },
        { L"  左键拖拽     图片放大后平移画面", 0 },
        { L"  左键双击     100% 原始比例 / 适应窗口 切换", 0 },
        { L"  右键 / \u2190 \u2192  上一张 / 下一张", 0 },
        { L"  F / 1        适应窗口 / 100%", 0 },
        { L"  + / -        放大 / 缩小", 0 },
        { L"", 0 },
        { L"窗口", 1 },
        { L"  拖动顶部信息栏移动窗口，双击信息栏最大化 / 恢复", 0 },
        { L"  拖到屏幕边缘自动排版，Win+\u2190 / \u2192 占据左 / 右半屏", 0 },
        { L"  P 置顶 / 取消置顶，F11 全屏，Esc 退出", 0 },
        { L"", 0 },
        { L"文件关联", 1 },
        { L"  Ctrl+A 注册关联    Ctrl+D 取消关联", 0 },
    };

    Gdiplus::Font titleFont(L"Microsoft YaHei", 24.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::Font subFont(L"Microsoft YaHei", 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font headFont(L"Microsoft YaHei", 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::Font bodyFont(L"Microsoft YaHei", 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

    float totalHeight = 0.0f;
    for (const HelpLine& line : kLines) {
        totalHeight += (line.style == 2) ? 46.0f : ((line.style == 1) ? 34.0f : 25.0f);
    }

    const float contentHeight = static_cast<float>(content.bottom - content.top);
    float y = (contentHeight > totalHeight)
                  ? static_cast<float>(content.top) + (contentHeight - totalHeight) / 2.0f
                  : static_cast<float>(content.top) + 12.0f;
    const float x = static_cast<float>(content.left) + 46.0f;

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    for (const HelpLine& line : kLines) {
        float lineHeight = 25.0f;
        Gdiplus::Font* font = &bodyFont;
        Gdiplus::Color color(255, 190, 190, 198);

        switch (line.style) {
        case 2:
            font = &titleFont;
            color = Gdiplus::Color(255, 245, 245, 250);
            lineHeight = 46.0f;
            break;
        case 3:
            font = &subFont;
            color = Gdiplus::Color(255, 140, 175, 190);
            break;
        case 1:
            font = &headFont;
            color = Gdiplus::Color(255, 110, 195, 205);
            lineHeight = 34.0f;
            break;
        default:
            break;
        }

        if (line.text[0] != L'\0') {
            Gdiplus::SolidBrush brush(color);
            Gdiplus::RectF rect(x, y,
                                static_cast<float>(content.right - content.left) - 80.0f,
                                lineHeight);
            g.DrawString(line.text, -1, font, rect, &format, &brush);
        }
        y += lineHeight;
    }
}

void DrawFullscreenHint(Gdiplus::Graphics& g, const RECT& content) {
    const float width = 190.0f;
    const float height = 24.0f;
    Gdiplus::RectF rect(static_cast<float>(content.right) - width - 18.0f,
                        static_cast<float>(content.bottom) - height - 14.0f,
                        width, height);
    Gdiplus::Font font(L"Microsoft YaHei", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::SolidBrush brush(Gdiplus::Color(180, 210, 210, 215));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentFar);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"F11 / Esc  退出全屏", -1, &font, rect, &format, &brush);
}

} // namespace

// ---------------------------------------------------------------------------
// BackBuffer
// ---------------------------------------------------------------------------
BackBuffer::BackBuffer(HDC screen, int width, int height) {
    if (!screen || width <= 0 || height <= 0) {
        return;
    }
    memDc_ = ScopedMemDc(screen);
    if (!memDc_.get()) {
        return;
    }
    bitmap_.reset(CreateCompatibleBitmap(screen, width, height));
    if (bitmap_.get()) {
        oldBitmap_ = ::SelectObject(memDc_.get(), bitmap_.get());
    }
}

BackBuffer::~BackBuffer() {
    if (memDc_.get() && oldBitmap_) {
        ::SelectObject(memDc_.get(), oldBitmap_);
        oldBitmap_ = nullptr;
    }
}

void BackBuffer::Present(HDC target, int width, int height) const noexcept {
    if (target && memDc_.get() && bitmap_.get()) {
        ::BitBlt(target, 0, 0, width, height, memDc_.get(), 0, 0, SRCCOPY);
    }
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
double Renderer::FitZoom(const ImageDocument& doc, int contentWidth, int contentHeight) {
    if (doc.width == 0 || doc.height == 0 || contentWidth <= 0 || contentHeight <= 0) {
        return 1.0;
    }
    const double scaleX = static_cast<double>(contentWidth) / doc.width;
    const double scaleY = static_cast<double>(contentHeight) / doc.height;
    return (std::min)(scaleX, scaleY) * 0.98;
}

void Renderer::Draw(HDC dc,
                    const RECT& client,
                    const ImageDocument* doc,
                    const ViewTransform& view,
                    const std::wstring& infoText,
                    const ButtonRects& buttons,
                    BarButton hovered,
                    bool showBar,
                    bool maximized,
                    bool pinned,
                    bool fullscreen) {
    if (!dc) {
        return;
    }

    const int clientHeight = client.bottom - client.top;
    const int barHeight = showBar ? (std::min)(kTopBarHeight, clientHeight) : 0;

    Gdiplus::Graphics g(dc);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    // 纯黑背景
    g.Clear(Gdiplus::Color(255, 0, 0, 0));

    if (showBar) {
        // 顶部信息栏
        RECT bar{ client.left, client.top, client.right, client.top + barHeight };
        FillRect(g, bar, Gdiplus::Color(255, 38, 38, 40));
        Gdiplus::Pen divider(Gdiplus::Color(255, 18, 18, 20), 1.0f);
        g.DrawLine(&divider,
                   static_cast<float>(client.left), static_cast<float>(bar.bottom),
                   static_cast<float>(client.right), static_cast<float>(bar.bottom));

        Gdiplus::Font font(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 226, 226, 232));
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentNear);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

        const float textWidth = static_cast<float>(
            (std::max)(0L, buttons.fit.left - client.left - 14L));
        Gdiplus::RectF textRect(static_cast<float>(client.left) + 10.0f,
                                static_cast<float>(client.top),
                                textWidth,
                                static_cast<float>(barHeight));
        g.DrawString(infoText.c_str(), -1, &font, textRect, &format, &textBrush);

        DrawArrow(g, buttons.prev, hovered == BarButton::Prev, true);
        DrawArrow(g, buttons.next, hovered == BarButton::Next, false);
        DrawFitIcon(g, buttons.fit, hovered == BarButton::Fit);
        DrawMinimizeIcon(g, buttons.minimize, hovered == BarButton::Minimize);
        DrawMaximizeIcon(g, buttons.maximize, hovered == BarButton::Maximize, maximized);
        DrawPinIcon(g, buttons.pin, hovered == BarButton::Pin, pinned);
        DrawFullscreenIcon(g, buttons.fullscreen, hovered == BarButton::Fullscreen);
        DrawCloseIcon(g, buttons.close, hovered == BarButton::Close);
    }

    RECT content{ client.left, client.top + barHeight, client.right, client.bottom };
    if (content.right <= content.left || content.bottom <= content.top) {
        return;
    }

    if (!doc || !doc->valid()) {
        DrawHelp(g, content);
        if (fullscreen) {
            DrawFullscreenHint(g, content);
        }
        return;
    }

    // 图片绘制区域：裁剪到信息栏以下
    g.SetClip(Gdiplus::Rect(content.left, content.top,
                            content.right - content.left,
                            content.bottom - content.top));

    // 画质核心：双三次插值
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

    Gdiplus::Rect destination(
        static_cast<INT>(std::llround(view.offsetX)),
        static_cast<INT>(std::llround(view.offsetY)),
        (std::max)(1, static_cast<INT>(std::llround(doc->width * view.zoom))),
        (std::max)(1, static_cast<INT>(std::llround(doc->height * view.zoom))));

    g.DrawImage(doc->bitmap.get(), destination,
                0, 0, static_cast<INT>(doc->width), static_cast<INT>(doc->height),
                Gdiplus::UnitPixel);

    if (fullscreen) {
        g.ResetClip();
        DrawFullscreenHint(g, content);
    }
}

} // namespace pv
