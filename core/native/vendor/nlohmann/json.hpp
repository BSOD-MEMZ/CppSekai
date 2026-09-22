// 转发头 —— 这份文件从前是一份完整的 nlohmann/json.hpp 3.12.0（25,830 行），
// 与 third_party/nlohmann/json.hpp **逐字节相同**（md5 83e2e643e7ef52e95511044d07110148）。
//
// 为什么不直接删掉整个目录：core/native/src/mmw_preview.cpp:28 用相对路径
//     #include "../vendor/nlohmann/json.hpp"
// 钉住了这个位置（上游代码，AGENTS.md 说不改结构），删文件就编不过。
//
// 为什么必须合并成一份：两份 header-only 库各被不同的翻译单元包含
// （mmw_preview.cpp 走这里，game/SongSelect.cpp 与 core/native/mmw_port/JsonIO.h 走
// -isystem third_party），今天是同一份所以没事；但只要将来**只升其中一份**，两个 TU 里
// 就会出现不同的 nlohmann::json 定义，链到一起是**静默的未定义行为**，不是编译错误。
// nlohmann 自带的版本检查只在单个 TU 内 `#warning`，跨 TU 完全不报。
//
// 所以这里留一行转发，仓库里只剩 third_party 那份实体。
#include <nlohmann/json.hpp>
