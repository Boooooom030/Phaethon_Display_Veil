#pragma once
// image_store.h — 遮罩图片库：枚举文件夹 + 懒加载 GDI+ 位图
// 所有调用都发生在 server UI 线程（IPC 分发 / overlay 绘制 / 计时器），
// 因此不做加锁。
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>

namespace images {

class Store {
public:
    // 设置图片目录并枚举；dir 为空 = 清空（回退纯黑）。返回找到的图片数。
    // 目录不存在返回 0。
    size_t SetDir(const std::wstring& dir);

    void   Clear();

    bool               HasImages() const { return !paths_.empty(); }
    size_t             Count() const { return paths_.size(); }
    const std::wstring Dir() const { return dir_; }

    size_t Index() const { return index_; }
    void   Next();

    // 懒加载 index 对应位图；失败返回 nullptr（绘制回退纯黑）
    Gdiplus::Bitmap* Get(size_t index);

private:
    void DropCache();

    std::wstring              dir_;
    std::vector<std::wstring> paths_;
    size_t                    index_ = 0;
    size_t                    loadedIndex_ = static_cast<size_t>(-1);
    std::unique_ptr<Gdiplus::Bitmap> current_;
};

Store& GetStore();

} // namespace images
