# Picture Viewer

一个仅使用 **Win32 API + C++17** 实现的轻量级图片查看器，不依赖 MFC / Qt / wxWidgets 等任何第三方 UI 框架。

## 功能

- **无边框窗口**（`WS_POPUP`），自绘标题栏：最小化 / 最大化·恢复 / 全屏 / 关闭按钮，以及上一张 / 下一张 / 适应窗口按钮。
- 拖动标题栏空白处移动窗口，双击标题栏最大化/恢复；窗口四边和四角支持鼠标拖拽调整大小。
- 纯黑背景、居中显示、顶部深色信息栏（标题 / 缩放比例 / 文件名 / 分辨率 / 文件大小）。
- 未打开任何文件时，在黑色区域显示软件介绍与全部交互说明。
- GDI+ `InterpolationModeHighQualityBicubic` 双三次插值渲染。
- 鼠标滚轮以指针为锚点平滑缩放（带缓动动画）。
- 图片大于视口时按住左键拖拽平移，边界自动限制。
- 左键双击在「100% 原始比例」和「适应窗口」之间切换。
- 右键切换到同目录下一张；顶部 `‹` `›` 按钮或键盘 `←` `→` 切换上一张/下一张。
- 支持 JPG / PNG / BMP / GIF / TIFF / ICO / JXR；WEBP 通过 libwebp 或系统 WIC 编解码器支持。
- 支持拖放文件到窗口打开，支持命令行传入文件路径。
- 内置文件关联注册/注销（Ctrl+A / Ctrl+D），另附 `associate.reg`。

## 快捷键

| 操作 | 功能 |
| --- | --- |
| 滚轮 | 以指针为中心缩放 |
| 左键拖拽 | 平移 |
| 左键双击 | 100% / 适应窗口切换 |
| 右键 | 下一张 |
| 拖动标题栏 | 移动无边框窗口 |
| 双击标题栏 | 最大化 / 恢复 |
| `←` / `→` | 上一张 / 下一张 |
| `Home` / `End` | 第一张 / 最后一张 |
| `F` | 适应窗口 |
| `1` | 100% |
| `+` / `-` | 放大 / 缩小 |
| `F11` | 切换全屏（隐藏标题栏） |
| `Esc` | 退出全屏；非全屏时退出程序 |
| `Ctrl+A` | 注册文件关联 |
| `Ctrl+D` | 取消文件关联 |

## 工程结构

```
picviewer/
├── CMakeLists.txt
├── README.md
├── associate.reg
├── resources/
│   ├── picture_viewer.ico  # 多尺寸程序图标
│   └── picture_viewer.rc   # 图标 + 版本信息资源
├── tools/
│   └── make_icon.ps1       # 图标生成脚本
├── include/
│   ├── pv_common.h      # RAII 包装、GDI+/COM 生命周期、工具函数
│   ├── pv_image.h       # ImageDocument / ImageLoader / ImageList
│   ├── pv_render.h      # ViewTransform / BackBuffer / Renderer
│   ├── pv_viewer.h      # ViewerWindow（窗口与交互）
│   └── pv_registry.h    # 文件关联
└── src/
    ├── main.cpp         # wWinMain、消息循环
    ├── pv_image.cpp     # WIC / GDI+ / libwebp 解码与目录枚举
    ├── pv_render.cpp    # 双三次缩放绘制与信息栏
    ├── pv_viewer.cpp    # WndProc 与鼠标/键盘处理
    └── pv_registry.cpp  # 注册表操作
```

## 构建（Windows）

环境（本机）：
- Visual Studio 2026 Community，CMake 生成器 `Visual Studio 18 2026`
- CMake：`D:\Software\Dev\cmake-4.4.4-windows-x86_64\bin\cmake.exe`

```powershell
& "D:\Software\Dev\cmake-4.4.4-windows-x86_64\bin\cmake.exe" -S . -B build -G "Visual Studio 18 2026" -A x64
& "D:\Software\Dev\cmake-4.4.4-windows-x86_64\bin\cmake.exe" --build build --config Release
```

生成的可执行文件：`build/Release/PictureViewer.exe`

### 可选：启用 libwebp（推荐，保证 WEBP 支持）

```powershell
$env:VCPKG_ROOT = "D:\Projects\vcpkg"
$env:VCPKG_SSL_REVOKE_BEST_EFFORT = "1"
& "D:\Projects\vcpkg\vcpkg.exe" install libwebp:x64-windows

& "D:\Software\Dev\cmake-4.4.4-windows-x86_64\bin\cmake.exe" -S . -B build-webp `
  -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=D:/Projects/vcpkg/scripts/buildsystems/vcpkg.cmake
& "D:\Software\Dev\cmake-4.4.4-windows-x86_64\bin\cmake.exe" --build build-webp --config Release
```

生成的可执行文件：`build-webp/Release/PictureViewer.exe`（同目录会附带 `libwebp.dll` 等运行时）。

若未找到 libwebp，程序仍能通过 WIC 解码 WEBP（前提是系统安装了 WebP 编解码器，例如 Microsoft WebP Image Extensions）。

## 发布包

免安装便携版请从 GitHub Releases 下载：`https://github.com/malik1988/PictureViewer/releases`。解压后直接运行 `PictureViewer.exe`。包内包含 WEBP 运行时（`libwebp.dll`、`libsharpyuv.dll`）、`associate.reg`（文件关联）与使用说明。

## 关键实现说明

### 滚轮以指针为中心的缩放

缩放前先求出指针下方的图像坐标，缩放后再把该点放回指针位置：

```cpp
imageX = (pointerX - offsetX) / oldZoom;
imageY = (pointerY - offsetY) / oldZoom;
newOffsetX = pointerX - imageX * newZoom;
newOffsetY = pointerY - imageY * newZoom;
```

### 双三次插值

```cpp
graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
graphics.DrawImage(image, destRect, 0, 0, srcW, srcH, Gdiplus::UnitPixel);
```

### 资源管理

- `GdiplusLifetime` / `ComLifetime`：进程级初始化与逆序释放。
- `ScopedGdiObject` / `ScopedWindowDc` / `ScopedMemDc`：GDI 句柄与 HDC 的 RAII。
- `std::unique_ptr<Gdiplus::Image>`：图像对象智能指针。
- `ImageDocument::backingPixels` 声明在 `bitmap` 之前，确保像素缓冲区活得比引用它的 GDI+ Bitmap 更久。

### 文件关联

`pv_registry.cpp` 写入 `HKCU\Software\Classes`，无需管理员权限，写好以后调用 `SHChangeNotify(SHCNE_ASSOCCHANGED, ...)` 通知资源管理器刷新。
