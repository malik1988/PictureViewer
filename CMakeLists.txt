cmake_minimum_required(VERSION 3.20)

project(PictureViewer
    VERSION 1.0.0
    DESCRIPTION "Lightweight native Win32 image viewer"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 只在 Windows 下构建
if(NOT WIN32)
    message(FATAL_ERROR "PictureViewer requires Windows (Win32 API).")
endif()

add_executable(PictureViewer WIN32
    src/main.cpp
    src/pv_image.cpp
    src/pv_render.cpp
    src/pv_viewer.cpp
    src/pv_registry.cpp
    resources/picture_viewer.rc
)

set_target_properties(PictureViewer PROPERTIES
    OUTPUT_NAME "PictureViewer"
)

target_include_directories(PictureViewer PRIVATE include)

target_compile_definitions(PictureViewer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(PictureViewer PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(PictureViewer PRIVATE -Wall -Wextra)
endif()

# 系统库：GDI+、WIC、COM、Shell、Shlwapi
target_link_libraries(PictureViewer PRIVATE
    gdiplus
    windowscodecs
    ole32
    shell32
    shlwapi
    user32
    gdi32
    advapi32
    comctl32
)

# WEBP 支持为可选依赖：
#   JPG/PNG/BMP/GIF/TIFF/ICO/JXR 由 WIC + GDI+ 原生支持；
#   WEBP 优先走系统安装的 WIC WebP 编解码器，找不到时回退到 libwebp。
# 安装方式（可选）：
#   vcpkg install libwebp:x64-windows
# 配置时带上 vcpkg toolchain 即可自动启用。
option(PICVIEWER_WITH_WEBP "Enable libwebp decoder fallback for WEBP images" ON)

if(PICVIEWER_WITH_WEBP)
    # vcpkg 会导出 WebP::webp 这个带正确 Debug/Release 配置的导入目标。
    find_package(WebP CONFIG QUIET)

    if(TARGET WebP::webp)
        message(STATUS "PictureViewer: libwebp found (WebP::webp)")
        target_link_libraries(PictureViewer PRIVATE WebP::webp)
        target_compile_definitions(PictureViewer PRIVATE PICVIEWER_HAVE_LIBWEBP=1)
    else()
        find_path(WEBP_INCLUDE_DIR webp/decode.h)
        find_library(WEBP_LIBRARY NAMES webp libwebp)

        if(WEBP_INCLUDE_DIR AND WEBP_LIBRARY)
            message(STATUS "PictureViewer: libwebp found -> ${WEBP_LIBRARY}")
            target_include_directories(PictureViewer PRIVATE ${WEBP_INCLUDE_DIR})
            target_link_libraries(PictureViewer PRIVATE optimized ${WEBP_LIBRARY})
            target_compile_definitions(PictureViewer PRIVATE PICVIEWER_HAVE_LIBWEBP=1)
        else()
            message(STATUS "PictureViewer: libwebp NOT found; WEBP relies on installed WIC codec.")
        endif()
    endif()
endif()
