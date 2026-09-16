// CppSekai - UTF-8 <-> std::filesystem::path, explicitly.
//
// On Windows the narrow side of fs::path is the *local ANSI code page*:
// constructing a path from a std::string decodes it with CP_ACP and, if the code
// page cannot represent one of the characters, throws
//
//     filesystem_error: __char_to_wide: Illegal byte sequence
//
// Nothing catches that, so the process just dies - a chart named
// "初音ミク🎵_master.sus" aborted the scan on a machine whose ACP is cp936. It is
// the same bug that used to take chartdl.exe down when a kana was typed into its
// search box (see AGENTS.md).
//
// So instead of implicit conversions, two rules:
//
//   * everything the game keeps in a std::string is UTF-8. That is already the
//     convention everywhere else - SDL_GetBasePath(), the command line
//     (utf8Argv) and nlohmann::json all hand out UTF-8 - so this is the one
//     encoding all of them agree on (and it is what miniaudio and stb_image
//     expect once STBI_WINDOWS_UTF8 is defined);
//   * crossing the boundary goes through toPath() / fromPath(), never through
//     fs::path(std::string) or path::string().
//
//   fs::path p = path_utf8::toPath(entry.susPath);
//   entry.susPath = path_utf8::fromPath(p);
#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// windows.h defines min/max as macros unless this is set, and the game uses
// std::min / std::max everywhere.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace path_utf8
{
#ifdef _WIN32
inline std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

inline std::string narrow(const std::wstring& wide)
{
    if (wide.empty()) {
        return std::string();
    }
    const int len = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

// fs::path -> UTF-8 text (what every std::string in the game holds).
inline std::string fromPath(const std::filesystem::path& path)
{
    return narrow(path.native());
}

// UTF-8 text -> fs::path (what every filesystem call needs).
inline std::filesystem::path toPath(const std::string& utf8)
{
    return std::filesystem::path(widen(utf8));
}
#else
inline std::string fromPath(const std::filesystem::path& path)
{
    return path.string();
}

inline std::filesystem::path toPath(const std::string& path)
{
    return std::filesystem::path(path);
}
#endif
} // namespace path_utf8
