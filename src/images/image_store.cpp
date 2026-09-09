#include "image_store.h"
#include "../util/logger.h"
#include "../util/text.h"

namespace images {

namespace {
const wchar_t* kExts[] = { L".jpg", L".jpeg", L".png", L".bmp", L".gif" };

bool HasImageExt(const std::wstring& name)
{
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    const std::wstring ext = util::ToLower(name.substr(dot));
    for (const wchar_t* e : kExts)
        if (ext == e) return true;
    return false;
}
} // namespace

Store& GetStore()
{
    static Store g;
    return g;
}

void Store::DropCache()
{
    current_.reset();
    loadedIndex_ = static_cast<size_t>(-1);
}

void Store::Clear()
{
    DropCache();
    paths_.clear();
    dir_.clear();
    index_ = 0;
}

size_t Store::SetDir(const std::wstring& dirIn)
{
    Clear();

    std::wstring d = dirIn;
    // 去掉尾部反斜杠 / 引号残留
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'"'))
        d.pop_back();
    if (d.empty()) return 0;

    const DWORD attr = GetFileAttributesW(d.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        util::Logger::Instance().Error(L"image dir not found: " + d);
        return 0;
    }

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring name = fd.cFileName;
            if (HasImageExt(name))
                paths_.push_back(d + L"\\" + name);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    dir_ = d;
    util::Logger::Instance().Info(L"Image folder set: " + d + L" (" +
                                  std::to_wstring(paths_.size()) + L" file(s))");
    return paths_.size();
}

void Store::Next()
{
    if (paths_.empty()) return;
    index_ = (index_ + 1) % paths_.size();
}

Gdiplus::Bitmap* Store::Get(size_t index)
{
    if (index >= paths_.size()) return nullptr;
    if (loadedIndex_ == index) return current_.get();

    DropCache();
    current_.reset(Gdiplus::Bitmap::FromFile(paths_[index].c_str()));
    loadedIndex_ = index;
    if (!current_ || current_->GetLastStatus() != Gdiplus::Ok)
    {
        util::Logger::Instance().Warn(L"failed to load image: " + paths_[index]);
        current_.reset();
    }
    return current_.get();
}

} // namespace images
