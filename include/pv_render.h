#pragma once

#include "pv_common.h"
#include "pv_image.h"

namespace pv {

enum class BarButton {
    None = -1,
    Prev = 0,
    Next = 1,
    Fit = 2,
    Minimize = 3,
    Maximize = 4,
    Fullscreen = 5,
    Close = 6,
};

struct ViewTransform {
    double zoom = 1.0;
    // 图片左上角在客户区坐标系中的位置（单位为像素）。
    double offsetX = 0.0;
    double offsetY = 0.0;
};

struct ButtonRects {
    RECT prev{};
    RECT next{};
    RECT fit{};
    RECT minimize{};
    RECT maximize{};
    RECT fullscreen{};
    RECT close{};
};

// 离屏缓冲，避免拖拽/缩放时闪烁。
class BackBuffer {
public:
    BackBuffer(HDC screen, int width, int height);
    ~BackBuffer();
    BackBuffer(const BackBuffer&) = delete;
    BackBuffer& operator=(const BackBuffer&) = delete;

    HDC Dc() const noexcept { return memDc_.get(); }
    void Present(HDC target, int width, int height) const noexcept;

private:
    ScopedMemDc memDc_;
    ScopedGdiObject bitmap_;
    HGDIOBJ oldBitmap_ = nullptr;
};

class Renderer {
public:
    static constexpr int kTopBarHeight = 32;

    static void Draw(HDC dc,
                     const RECT& client,
                     const ImageDocument* doc,
                     const ViewTransform& view,
                     const std::wstring& infoText,
                     const ButtonRects& buttons,
                     BarButton hovered,
                     bool showBar,
                     bool maximized,
                     bool fullscreen);

    static double FitZoom(const ImageDocument& doc, int contentWidth, int contentHeight);
};

} // namespace pv
