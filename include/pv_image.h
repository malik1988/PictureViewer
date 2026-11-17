#pragma once

#include "pv_common.h"

namespace pv {

// 一张已解码的图片及其元信息。
// 注意：backingPixels 必须先于 bitmap 销毁，因此声明顺序很重要。
struct ImageDocument {
    std::wstring path;
    std::wstring fileName;
    ULONGLONG fileSize = 0;
    std::wstring format;
    UINT width = 0;
    UINT height = 0;

    std::vector<BYTE> backingPixels;
    std::unique_ptr<Gdiplus::Image> bitmap;

    bool valid() const noexcept {
        return bitmap && bitmap->GetLastStatus() == Gdiplus::Ok;
    }
};

class ImageLoader {
public:
    static std::unique_ptr<ImageDocument> Load(const std::wstring& path);
};

// 枚举同目录下的常见图片文件，按文件名（不区分大小写）排序。
class ImageList {
public:
    bool SetFile(const std::wstring& filePath);
    bool Empty() const noexcept { return files_.empty(); }
    std::size_t Size() const noexcept { return files_.size(); }
    std::size_t Index() const noexcept { return index_; }
    const std::wstring& Current() const { return files_[index_]; }

    bool Select(std::size_t index);
    bool Next();
    bool Previous();

private:
    std::vector<std::wstring> files_;
    std::size_t index_ = 0;
};

} // namespace pv
