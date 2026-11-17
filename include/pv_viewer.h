#pragma once

#include "pv_common.h"
#include "pv_image.h"
#include "pv_render.h"

namespace pv {

class ViewerWindow {
public:
    static bool Register(HINSTANCE instance);
    static HWND Create(HINSTANCE instance, const std::wstring& initialFile, int showCommand);

private:
    static constexpr const wchar_t* kClassName = L"PictureViewerWindow";
    static constexpr UINT_PTR kAnimationTimerId = 1;
    static constexpr double kAnimationStep = 0.30;
    static constexpr double kMinZoom = 0.02;
    static constexpr double kMaxZoom = 64.0;
    static constexpr int kResizeBorder = 6;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnDestroy();
    void OnSize(int width, int height);
    void OnPaint();
    void OnMouseWheel(int delta, POINT pt);
    void OnLButtonDown(POINT pt);
    void OnMouseMove(POINT pt);
    void OnLButtonUp();
    void OnLButtonDblClk(POINT pt);
    void OnRButtonUp(POINT pt);
    void OnKeyDown(WPARAM key);
    void OnTimer();
    void OnDropFiles(HDROP drop);
    bool OnSetCursor(POINT pt);
    LRESULT OnNcHitTest(LPARAM lParam);

    RECT ContentRect() const;
    BarButton HitTestBar(POINT pt) const;
    void RecomputeBarLayout(int clientWidth);
    void UpdateInfoText();

    bool OpenFile(const std::wstring& path);
    bool Advance(int direction);
    void FitToWindow(bool animate);
    void ToggleActualSize();
    void ZoomAt(POINT pt, double factor);
    void StartAnimation(double zoom, double offsetX, double offsetY);
    void StopAnimation();
    void ClampView(double& zoom, double& offsetX, double& offsetY) const;
    void Invalidate() const;

    void ToggleMaximize();
    void ToggleFullscreen();
    void TogglePin();
    void ApplyTopmostState();
    void ApplyPlacement(const RECT& rect, HWND insertAfter, UINT flags);
    static RECT MonitorAreaFor(HWND hwnd);

    HWND hwnd_ = nullptr;
    std::wstring initialFile_;
    std::wstring infoText_;
    std::unique_ptr<ImageList> images_;
    std::unique_ptr<ImageDocument> document_;

    ViewTransform view_;
    ViewTransform targetView_;
    bool animating_ = false;
    bool fitMode_ = true;

    bool dragging_ = false;
    bool dragMoved_ = false;
    bool mouseTracking_ = false;
    POINT dragStart_{};
    double dragOffsetStartX_ = 0.0;
    double dragOffsetStartY_ = 0.0;

    bool maximized_ = false;
    bool pinned_ = false;
    bool fullscreen_ = false;
    RECT fullscreenRestoreRect_{};
    bool fullscreenWasMaximized_ = false;

    BarButton hoveredButton_ = BarButton::None;
    ButtonRects buttons_;
    std::unique_ptr<BackBuffer> backBuffer_;
    int clientWidth_ = 0;
    int clientHeight_ = 0;
};

} // namespace pv
