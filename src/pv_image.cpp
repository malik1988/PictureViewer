#include "pv_image.h"

#include <shlwapi.h>

#ifdef PICVIEWER_HAVE_LIBWEBP
#include <webp/decode.h>
#endif

namespace pv {
namespace {

bool IsSupportedExtension(const std::wstring& ext) {
    static const wchar_t* kExts[] = {
        L"JPG", L"JPEG", L"PNG", L"BMP", L"GIF",
        L"TIF", L"TIFF", L"WEBP", L"ICO", L"JXR", L"JPE"
    };
    for (const wchar_t* e : kExts) {
        if (ext == e) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// WIC 解码：Windows 原生支持 JPG/PNG/BMP/GIF/TIFF/ICO/JXR，
// 以及系统安装的额外编解码器（例如 WebP Image Extensions）。
// 统一转成 32bpp BGRA，与 GDI+ PixelFormat32bppARGB 的内存布局一致。
// ---------------------------------------------------------------------------
bool DecodeWithWic(const std::wstring& path, ImageDocument& doc) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) {
        return false;
    }
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut))) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) {
        return false;
    }

    const UINT stride = width * 4u;
    const std::uint64_t total = static_cast<std::uint64_t>(stride) * height;
    if (total == 0 || total > 0xFFFFFFFFull) {
        return false;
    }

    std::vector<BYTE> pixels(static_cast<std::size_t>(total));
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(total), pixels.data()))) {
        return false;
    }

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(
        static_cast<INT>(width), static_cast<INT>(height),
        static_cast<INT>(stride), PixelFormat32bppARGB, pixels.data());
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    doc.width = width;
    doc.height = height;
    doc.backingPixels = std::move(pixels);
    doc.bitmap = std::move(bitmap);
    return true;
}

// ---------------------------------------------------------------------------
// 兜底解码：GDI+ 自己的编解码器（某些 ICO/EMF/WMF 场景）
// ---------------------------------------------------------------------------
bool DecodeWithGdiPlus(const std::wstring& path, ImageDocument& doc) {
    auto image = std::make_unique<Gdiplus::Image>(path.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    doc.width = image->GetWidth();
    doc.height = image->GetHeight();
    doc.bitmap = std::move(image);
    return true;
}

#ifdef PICVIEWER_HAVE_LIBWEBP
// ---------------------------------------------------------------------------
// libwebp 兜底解码：不依赖系统是否安装 WebP WIC 编解码器。
// ---------------------------------------------------------------------------
bool DecodeWithWebP(const std::wstring& path, ImageDocument& doc) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > 0x7FFFFFFF) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<BYTE> fileData(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool readOk = ReadFile(hFile, fileData.data(), static_cast<DWORD>(fileData.size()),
                                 &read, nullptr) && read == fileData.size();
    CloseHandle(hFile);
    if (!readOk) {
        return false;
    }

    int width = 0;
    int height = 0;
    if (WebPGetInfo(fileData.data(), fileData.size(), &width, &height) == 0 ||
        width <= 0 || height <= 0) {
        return false;
    }

    int outWidth = 0;
    int outHeight = 0;
    BYTE* decoded = WebPDecodeBGRA(fileData.data(), fileData.size(), &outWidth, &outHeight);
    if (!decoded) {
        return false;
    }

    const std::size_t bytes = static_cast<std::size_t>(outWidth) * 4u * outHeight;
    std::vector<BYTE> pixels(decoded, decoded + bytes);
    WebPFree(decoded);

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(
        outWidth, outHeight, outWidth * 4, PixelFormat32bppARGB, pixels.data());
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    doc.width = static_cast<UINT>(outWidth);
    doc.height = static_cast<UINT>(outHeight);
    doc.backingPixels = std::move(pixels);
    doc.bitmap = std::move(bitmap);
    return true;
}
#endif

} // namespace

std::unique_ptr<ImageDocument> ImageLoader::Load(const std::wstring& path) {
    auto doc = std::make_unique<ImageDocument>();
    doc->path = path;
    doc->fileName = ::PathFindFileNameW(path.c_str());
    doc->format = ExtensionOf(path);

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        doc->fileSize = (static_cast<ULONGLONG>(attributes.nFileSizeHigh) << 32) |
                        attributes.nFileSizeLow;
    }

    // WEBP 先试 libwebp（若编译进来了），这样不依赖系统是否装了 WIC 编解码器。
    bool loaded = false;
#ifdef PICVIEWER_HAVE_LIBWEBP
    if (doc->format == L"WEBP") {
        loaded = DecodeWithWebP(path, *doc);
    }
#endif
    if (!loaded) {
        loaded = DecodeWithWic(path, *doc);
    }
    if (!loaded) {
        loaded = DecodeWithGdiPlus(path, *doc);
    }

    return loaded ? std::move(doc) : nullptr;
}

bool ImageList::SetFile(const std::wstring& filePath) {
    files_.clear();
    index_ = 0;

    wchar_t directory[MAX_PATH]{};
    wchar_t fileName[MAX_PATH]{};
    if (_wsplitpath_s(filePath.c_str(), nullptr, 0, directory, MAX_PATH,
                      fileName, MAX_PATH, nullptr, 0) != 0) {
        return false;
    }

    const std::wstring pattern = std::wstring(directory) + L"\\*";
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }

    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!IsSupportedExtension(ExtensionOf(findData.cFileName))) {
            continue;
        }
        files_.push_back(std::wstring(directory) + L"\\" + findData.cFileName);
    } while (FindNextFileW(find, &findData));
    FindClose(find);

    std::sort(files_.begin(), files_.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(::PathFindFileNameW(a.c_str()),
                        ::PathFindFileNameW(b.c_str())) < 0;
    });

    auto it = std::find_if(files_.begin(), files_.end(), [&](const std::wstring& candidate) {
        return _wcsicmp(candidate.c_str(), filePath.c_str()) == 0;
    });
    if (it != files_.end()) {
        index_ = static_cast<std::size_t>(it - files_.begin());
    } else {
        // 当前文件即使扩展名不在白名单里，也保证它出现在列表中。
        files_.insert(files_.begin(), filePath);
        index_ = 0;
    }
    return !files_.empty();
}

bool ImageList::Select(std::size_t index) {
    if (files_.empty() || index >= files_.size()) {
        return false;
    }
    index_ = index;
    return true;
}

bool ImageList::Next() {
    if (files_.empty()) {
        return false;
    }
    index_ = (index_ + 1) % files_.size();
    return true;
}

bool ImageList::Previous() {
    if (files_.empty()) {
        return false;
    }
    index_ = (index_ + files_.size() - 1) % files_.size();
    return true;
}

} // namespace pv
