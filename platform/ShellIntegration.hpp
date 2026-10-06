// ---------------------------------------------------------------------------
// 跟 Windows 外壳打交道的那几件"要写点系统状态"的事。
//
// 为什么单独一个文件：这些代码要么动注册表、要么手写 COM 的 vtable，跟渲染/音频
// 那几层没关系，塞进 main.cpp 只会让那个文件更难看（它已经太大了）。
//
// 两条原则：
//   * **只写 HKCU**（HKEY_CURRENT_USER\Software\Classes）—— 不需要管理员，也不
//     影响别的用户；卸载时留得下、撤得掉。
//   * **每一步都可能失败**（老系统 / 组策略 / 权限），失败就返回 false 并打日志，
//     绝不让它影响游戏本身。
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace platform::shell
{

// ---------------------------------------------------------------------------
// .sus 谱面文件关联（设置 > 系统 > 关联 .sus 谱面文件）
//
// 双击一个 .sus 就进游戏（走 --sus <路径>，跟命令行那条完全一样）。
// 写的是 HKCU\Software\Classes，三处：
//
//   .sus                       (默认) = "CppSekai.Chart"
//   CppSekai.Chart             (默认) = "SUS 谱面 (CppSekai)"
//   CppSekai.Chart\DefaultIcon (默认) = "<exe>,0"
//   CppSekai.Chart\shell\open\command (默认) = "\"<exe>\" --sus \"%1\""
//
// 为什么不做成"所有 .sus 都归我"的霸道写法：HKCU\Software\Classes\.sus 是**每用户**
// 的，别人装了解析器也互不干扰，撤掉就回到系统默认。
// ---------------------------------------------------------------------------

// exePath = 本程序 exe 的绝对路径（\ 分隔）。
bool associateSus(const std::wstring& exePath);
// 撤销：只在 .sus 确实指向我们写的那个 ProgID 时才删，不会把别的程序的关联拆掉。
bool disassociateSus();
// 现在关联着吗（读注册表，启动时查一次就够）。
bool susAssociated();

// ---------------------------------------------------------------------------
// Jump List（右键任务栏图标出来的那个菜单）
//
// 两个自定义分类：收藏 / 最近播放，各自列几条曲子，点一下直接开打
// （同样走 --sus <路径>，所以曲目路径必须能被本机读到）。
// ---------------------------------------------------------------------------

// 一条跳转项：`title` 是列表里显示的字，`susPath` 是要打开的谱面。
struct JumpEntry
{
    std::wstring title;
    std::wstring susPath;
    std::wstring detail; // 副标题（难度 / 歌手），没有就留空
};

// 重建整份 Jump List（先清后建，幂等）。categories 最多两个键：
// "收藏" 和 "最近播放"。任何一步失败都会打日志并返回 false，界面不受影响。
// 空列表也接受：那就只是把旧的清掉。
bool rebuildJumpList(const std::wstring& exePath,
    const std::vector<std::pair<std::wstring, std::vector<JumpEntry>>>& categories);

} // namespace platform::shell
