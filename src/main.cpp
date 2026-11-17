#include "pv_common.h"
#include "pv_viewer.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // 高 DPI 感知：避免位图拉伸导致模糊。
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        using SetDpiAwareFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = reinterpret_cast<SetDpiAwareFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    pv::GdiplusLifetime gdiplus;
    pv::ComLifetime com;

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring initialFile;
    if (argv && argc > 1) {
        initialFile = argv[1];
    }
    if (argv) {
        ::LocalFree(argv);
    }

    if (!pv::ViewerWindow::Register(instance)) {
        ::MessageBoxW(nullptr, L"窗口类注册失败。", L"Picture Viewer", MB_ICONERROR);
        return 1;
    }

    if (!pv::ViewerWindow::Create(instance, initialFile, showCommand)) {
        return 1;
    }

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
