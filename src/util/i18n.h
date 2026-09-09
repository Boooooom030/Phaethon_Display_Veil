#pragma once
// i18n.h — 极简双语字符串表（中文/英文）
// 语言来源：环境变量 PRIVACY_SCREEN_LANG (zh/en) > 系统默认 UI 语言。
namespace i18n {

enum class S {
    TrayTip,          // 托盘提示
    EnablePrivacy,    // 顶部开关
    SelectScreen,     // 屏幕子菜单标题
    AllScreens,       // 子菜单：全部屏幕
    MonitorPrefix,    // "显示器 " / "Monitor "
    PrimaryTag,       // " [主]" / " [Primary]"
    ChooseImages,     // 选择图片文件夹
    ChangeImages,     // 更换图片文件夹（已有图片库时）
    BackToBlack,      // 恢复纯黑
    Status,           // 状态
    Exit,             // 退出
    FolderDialogTitle,// 文件夹对话框标题
};

// 启动时调用一次（语言检测）
void Init();

// 取当前语言字符串
const wchar_t* Str(S id);

} // namespace i18n
