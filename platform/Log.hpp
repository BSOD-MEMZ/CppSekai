// 日志分类开关（设置 > 系统 > 日志输出）。
//
// 所有带 [tag] 前缀的诊断输出都走 logf()，它先查掩码再写 stdout。默认全开
// （行为和以前一模一样）；关掉某一类就真的不产生了，省的是磁盘 IO —— 那些
// 每帧/每秒刷的行在慢盘上很可观。
//
// 分类是按"出问题时你大概会一起看"来分的，不是按代码模块：
//   系统      启动、设置、窗口、菜单、多开、媒体键
//   选曲      选曲界面、下载器、猜歌、收藏、删除
//   剧情      教学与剧情脚本
//   演奏      演奏、暂停、结算、判定
//   音频      miniaudio 播放与音效
//   输入      手柄、键盘、触摸、拖放
//   渲染      OpenGL、帧统计、UI 尺寸
//   多人游玩  房间同步
#pragma once

namespace platform
{
enum LogCat
{
    LogSystem = 0,
    LogSelect,
    LogStory,
    LogPlay,
    LogAudio,
    LogInput,
    LogRender,
    LogParty,
    LogCatCount
};

// 默认全开 = 0xFF。
void setLogMask(unsigned mask);
unsigned logMask();
bool logOn(LogCat cat);
// printf 的分类版本：掩码里没有这一类的位就整条丢掉（连格式化都不做）。
void logf(LogCat cat, const char* fmt, ...);
}
