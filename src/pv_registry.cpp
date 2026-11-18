#include "pv_registry.h"

namespace pv {
namespace {

constexpr const wchar_t* kProgId = L"PictureViewer.Image.1";
constexpr const wchar_t* kProgDescription = L"Picture Viewer File";

static const wchar_t* const kExtensions[] = {
    // 图片
    L".jpg", L".jpeg", L".jpe", L".png", L".bmp",
    L".gif", L".tif", L".tiff", L".webp", L".ico", L".jxr",
    // 3D 模型
    L".obj", L".stl", L".ply", L".off", L".glb", L".gltf"
};

bool IsImageExtension(const wchar_t* ext) {
    static const wchar_t* const kImageExtensions[] = {
        L".jpg", L".jpeg", L".jpe", L".png", L".bmp",
        L".gif", L".tif", L".tiff", L".webp", L".ico", L".jxr"
    };
    for (const wchar_t* image : kImageExtensions) {
        if (_wcsicmp(ext, image) == 0) {
            return true;
        }
    }
    return false;
}

// 写入 REG_SZ；键不存在时自动创建。
bool SetStringValue(HKEY parent, const wchar_t* subKey, const wchar_t* name,
                    const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(parent, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool DeleteTree(HKEY parent, const wchar_t* subKey) {
    const LSTATUS status = RegDeleteTreeW(parent, subKey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

std::wstring QuoteProgramPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(L"\"") + path + L"\"";
}

} // namespace

bool RegisterAssociations() {
    const std::wstring program = QuoteProgramPath();
    const std::wstring classes = std::wstring(L"Software\\Classes\\") + kProgId;

    if (!SetStringValue(HKEY_CURRENT_USER, classes.c_str(), nullptr, kProgDescription)) {
        return false;
    }

    // 默认图标使用第一枚应用图标；若无资源图标，系统回退到 EXE 图标。
    const std::wstring defaultIcon = program + L",0";
    if (!SetStringValue(HKEY_CURRENT_USER, (classes + L"\\DefaultIcon").c_str(),
                        nullptr, defaultIcon)) {
        return false;
    }

    const std::wstring openCommand = program + L" \"%1\"";
    if (!SetStringValue(HKEY_CURRENT_USER, (classes + L"\\shell\\open\\command").c_str(),
                        nullptr, openCommand)) {
        return false;
    }
    if (!SetStringValue(HKEY_CURRENT_USER, (classes + L"\\shell\\open").c_str(),
                        L"Icon", program)) {
        return false;
    }

    for (const wchar_t* ext : kExtensions) {
        const std::wstring key = std::wstring(L"Software\\Classes\\") + ext;
        if (!SetStringValue(HKEY_CURRENT_USER, key.c_str(), nullptr, kProgId)) {
            return false;
        }
        // 图片写入 PerceivedType=image 便于资源管理器识别；3D 模型不写。
        if (IsImageExtension(ext)) {
            SetStringValue(HKEY_CURRENT_USER, key.c_str(), L"PerceivedType", L"image");
        }
    }

    // 通知 Shell 刷新关联缓存。
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool UnregisterAssociations() {
    DeleteTree(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + kProgId).c_str());

    for (const wchar_t* ext : kExtensions) {
        const std::wstring key = std::wstring(L"Software\\Classes\\") + ext;
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &hKey) !=
            ERROR_SUCCESS) {
            continue;
        }
        wchar_t value[256]{};
        DWORD size = sizeof(value);
        const LSTATUS status = RegQueryValueExW(hKey, nullptr, nullptr, nullptr,
                                                reinterpret_cast<BYTE*>(value), &size);
        RegCloseKey(hKey);
        if (status == ERROR_SUCCESS && value == std::wstring(kProgId)) {
            RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

} // namespace pv
