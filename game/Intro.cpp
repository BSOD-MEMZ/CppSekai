// CppSekai - opening intro card (see Intro.hpp).
// 1:1 port of drawOpeningIntroOverlay() / introCardAlpha() /
// openingPlayfieldVisibility() / loadIntroFonts() from
// core/native/src/mmw_overlay_player.cpp (sekai-mmw-preview-web, AGPL-3.0).
#include "Intro.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace game
{
namespace
{
    // ---- Upstream constants (mmw_overlay_player.cpp) --------------------
    constexpr float INTRO_ENTER_FADE_SEC = 0.52f; // unused upstream, kept for reference
    constexpr float INTRO_EXIT_FADE_SEC = 0.56f;
    constexpr float INTRO_BG_ALPHA = 0.82f;
    constexpr float INTRO_GRAD_ALPHA = 0.10f;
    constexpr float INTRO_GRAD_START_SEC = 1.0f;
    constexpr float INTRO_GRAD_DURATION_SEC = 2.0f;
    constexpr float INTRO_GRAD_DRAW_WIDTH = 2001.0f;
    constexpr float INTRO_GRAD_DRAW_HEIGHT = 1125.0f;
    constexpr float INTRO_GRAD_START_Y = 1500.0f;
    constexpr float INTRO_GRAD_END_Y = 0.0f;
    constexpr float INTRO_COVER_LEFT_PX = 148.0f;
    constexpr float INTRO_COVER_BOTTOM_PX = 104.0f;
    constexpr float INTRO_COVER_SIZE_PX = 350.0f;
    constexpr float INTRO_TEXT_LEFT_WITH_COVER_PX = 540.0f;
    constexpr float INTRO_TEXT_LEFT_NO_COVER_PX = 186.0f;
    constexpr float INTRO_TEXT_BOTTOM_PX = 110.0f;
    constexpr float INTRO_TEXT_BLOCK_SHIFT_Y_PX = 26.0f;
    constexpr float INTRO_DIFF_LABEL_Y_OFFSET_PX = 9.0f;
    constexpr float INTRO_TITLE_DRAW_SIZE_PX = 38.0f;
    constexpr float INTRO_TITLE_LETTER_SPACING_PX = 5.0f;
    constexpr float INTRO_DIFF_DRAW_SIZE_PX = 28.0f;
    constexpr float INTRO_BODY_DRAW_SIZE_PX = 26.0f;
    constexpr float INTRO_DESC1_ROW_OFFSET_PX = 88.0f;
    constexpr float INTRO_DESC2_ROW_OFFSET_PX = 136.0f;

    ImFont* gTitleFont = nullptr;
    ImFont* gBodyFont = nullptr;
    ImFont* gDiffFont = nullptr;
    ImFont* gBoldFont = nullptr;
    ImFont* gCondFont = nullptr;

    float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }

    float easeOutQuad(float value)
    {
        const float clamped = clamp01(value);
        return 1.0f - (1.0f - clamped) * (1.0f - clamped);
    }

    std::string trimText(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
        return value;
    }

    std::string toUpper(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return value;
    }

    size_t utf8CodepointLength(unsigned char leadByte)
    {
        if ((leadByte & 0x80u) == 0) {
            return 1;
        }
        if ((leadByte & 0xE0u) == 0xC0u) {
            return 2;
        }
        if ((leadByte & 0xF0u) == 0xE0u) {
            return 3;
        }
        if ((leadByte & 0xF8u) == 0xF0u) {
            return 4;
        }
        return 1;
    }

    void drawSpacedUtf8Text(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 position, ImU32 color,
        const std::string& text, float extraSpacing, float maxWidth)
    {
        if (drawList == nullptr || font == nullptr || text.empty()) {
            return;
        }

        const char* cursor = text.c_str();
        const char* end = cursor + text.size();
        float x = position.x;
        const float maxX = maxWidth > 0.0f ? position.x + maxWidth : FLT_MAX;

        while (cursor < end) {
            const size_t glyphLength = std::min(
                utf8CodepointLength(static_cast<unsigned char>(*cursor)),
                static_cast<size_t>(end - cursor));
            const std::string glyph(cursor, glyphLength);
            const float glyphWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyph.c_str()).x;
            if (x + glyphWidth > maxX) {
                break;
            }
            drawList->AddText(font, fontSize, ImVec2(x, position.y), color, glyph.c_str());
            x += glyphWidth + extraSpacing;
            cursor += glyphLength;
        }
    }

    std::string normalizeDifficulty(std::string value)
    {
        value = toUpper(trimText(value));
        value.erase(std::remove_if(value.begin(), value.end(),
                        [](unsigned char c) { return std::isspace(c) != 0; }),
            value.end());
        if (value == "0") {
            return "EASY";
        }
        if (value == "1") {
            return "NORMAL";
        }
        if (value == "2") {
            return "HARD";
        }
        if (value == "3") {
            return "EXPERT";
        }
        if (value == "4") {
            return "MASTER";
        }
        if (value == "5") {
            return "APPEND";
        }
        if (value == "6") {
            return "ETERNAL";
        }
        return value;
    }

    std::string inferDifficultyFromSusPath(std::string susPath)
    {
        susPath = toUpper(susPath);
        const std::array<std::string, 7> ordered{
            "ETERNAL", "APPEND", "MASTER", "EXPERT", "HARD", "NORMAL", "EASY"
        };
        for (const auto& candidate : ordered) {
            if (susPath.find(candidate) != std::string::npos) {
                return candidate;
            }
        }
        return "";
    }

    int alphaByte(float v)
    {
        return static_cast<int>(std::lround(clamp01(v) * 255.0f));
    }
} // namespace

#ifdef _WIN32
    std::string wideToUtf8(const std::wstring& value)
    {
        if (value.empty()) {
            return {};
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(bytes > 1 ? bytes - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
        return utf8;
    }

    std::wstring trimWide(std::wstring value)
    {
        while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) {
            value.pop_back();
        }
        return value;
    }

    // Looks a face name up in the Fonts registry key and returns the full path
    // of the file that provides it. Registry keys group several faces with
    // " & " ("Yu Gothic Medium & Yu Gothic UI Regular (TrueType)") and append a
    // technology suffix, so the names are matched part-by-part.
    std::string findFontFile(const std::wstring& face, int matchMode)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                0, KEY_READ, &key)
            != ERROR_SUCCESS) {
            return {};
        }
        std::string result;
        for (DWORD index = 0; result.empty(); ++index) {
            wchar_t name[512]{};
            wchar_t data[512]{};
            DWORD nameSize = 512;
            DWORD dataSize = sizeof(data);
            DWORD type = 0;
            if (RegEnumValueW(key, index, name, &nameSize, nullptr, &type,
                    reinterpret_cast<LPBYTE>(data), &dataSize)
                != ERROR_SUCCESS) {
                break;
            }
            if (type != REG_SZ) {
                continue;
            }
            std::wstring keyName(name);
            const size_t paren = keyName.find(L" (");
            if (paren != std::wstring::npos) {
                keyName = keyName.substr(0, paren);
            }
            size_t start = 0;
            bool matched = false;
            while (!matched && start <= keyName.size()) {
                const size_t amp = keyName.find(L'&', start);
                const std::wstring part =
                    trimWide(keyName.substr(start, amp == std::wstring::npos ? std::wstring::npos : amp - start));
                if (!part.empty()) {
                    if (matchMode == 0) {
                        matched = part == face;
                    } else if (matchMode == 1) {
                        matched = part == face + L" Regular";
                    } else {
                        matched = part.rfind(face + L" ", 0) == 0;
                    }
                }
                if (amp == std::wstring::npos) {
                    break;
                }
                start = amp + 1;
            }
            if (!matched) {
                continue;
            }
            std::wstring file(data);
            const size_t nul = file.find(L'\0');
            if (nul != std::wstring::npos) {
                file = file.substr(0, nul);
            }
            if (file.empty()) {
                continue;
            }
            // The Fonts key stores either a bare file name ("msyh.ttc") or a full
            // path ("C:\\Windows\\Fonts\\msyh.ttc", what Win7 tends to write for
            // fonts that arrived with an update / a language pack). Gluing "\Fonts\"
            // in front of the latter produced a path that cannot exist - the font
            // was then silently skipped, which is how a machine with perfectly good
            // CJK fonts ends up on ImGui's bitmap face.
            wchar_t windowsDir[MAX_PATH]{};
            GetWindowsDirectoryW(windowsDir, MAX_PATH);
            result = (file.size() > 1 && file[1] == L':')
                ? wideToUtf8(file)
                : wideToUtf8(windowsDir) + "\\Fonts\\" + wideToUtf8(file);
        }
        RegCloseKey(key);
        return result;
    }

    std::string resolveFontFile(const std::wstring& face)
    {
        // Prefer an exact face, then "<face> Regular", then any style of the
        // face ("Yu Gothic UI Regular" is fine for "Yu Gothic UI").
        for (int mode = 0; mode < 3; ++mode) {
            const std::string path = findFontFile(face, mode);
            if (!path.empty()) {
                return path;
            }
        }
        return {};
    }

    struct SystemFontCandidate
    {
        std::string path;
        std::string face;
    };

    // The face Windows itself uses for UI text first (so the game follows the
    // system), then the common CJK faces as a safety net: a latin-only UI font
    // such as Segoe UI would render every song title as tofu.
    std::vector<SystemFontCandidate> systemFontCandidates()
    {
        std::vector<SystemFontCandidate> candidates;
        auto push = [&](const std::string& path, const std::string& label) {
            if (path.empty()) {
                return;
            }
            for (const auto& existing : candidates) {
                if (existing.path == path) {
                    return; // same file reached by two names (msyh.ttc and all)
                }
            }
            candidates.push_back({path, label});
        };

        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        std::wstring faceName;
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != 0) {
            faceName = metrics.lfMessageFont.lfFaceName;
        }
        if (!faceName.empty()) {
            push(resolveFontFile(faceName), wideToUtf8(faceName));
        }
        // Japanese first (kana + kanji + latin in one file, the only CJK faces
        // on a plain JP install), then Chinese, then Korean.
        // Both the English and the localized registry names are listed because
        // the Fonts key is localized: zh-CN would work with either
        // "Microsoft YaHei" or "微软雅黑", but 宋体's English half is "SimSun"
        // and 黑体's entry may carry no English name at all.
        for (const wchar_t* fallback : {
                 L"Microsoft YaHei UI", L"Microsoft YaHei", L"微软雅黑",
                 L"Yu Gothic UI", L"Yu Gothic", L"Meiryo UI", L"Meiryo", L"MS Gothic",
                 L"MS UI Gothic", L"SimSun", L"宋体", L"SimHei", L"黑体",
                 L"Microsoft JhengHei", L"微軟正黑體", L"Malgun Gothic",
                 L"Noto Sans SC", L"Noto Sans JP" }) {
            push(resolveFontFile(fallback), wideToUtf8(fallback));
        }

        // Registry-free net, and the reason this works on a Win7 box at all.
        // The *value names* in HKLM\...\Fonts are what a lookup depends on, and
        // they differ per locale and per Windows version: Win7 has no
        // "Microsoft YaHei UI" entry (that face is Win8+), and on an English
        // install the message font is Segoe UI - latin only, rejected below.
        // The *file names*, on the other hand, have not changed since Vista, so
        // try those directly before giving up.
        {
            wchar_t windowsDir[MAX_PATH]{};
            GetWindowsDirectoryW(windowsDir, MAX_PATH);
            const std::wstring root = std::wstring(windowsDir) + L"\\Fonts\\";
            for (const wchar_t* file : {
                     L"msyh.ttc",     // Microsoft YaHei (Win7+)
                     L"msyh.ttf",     // Vista, or XP with the ClearType pack
                     L"meiryo.ttc",   // Meiryo (Vista+, JP)
                     L"msgothic.ttc", // MS Gothic (every JP install)
                     L"YuGothM.ttc",  // Yu Gothic (Win8.1+)
                     L"msjh.ttc",     // Microsoft JhengHei (zh-TW)
                     L"malgun.ttf",   // Malgun Gothic (ko-KR)
                     L"simhei.ttf",   // SimHei
                     L"simsun.ttc",   // SimSun (always installed, serif)
                     L"mingliu.ttc",  // MingLiU (zh-TW)
                     L"arialuni.ttf", // Arial Unicode MS (Office)
                 }) {
                const std::wstring widePath = root + file;
                if (GetFileAttributesW(widePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    continue;
                }
                push(wideToUtf8(widePath), wideToUtf8(file));
            }
        }
        return candidates;
    }
#endif

void loadIntroFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // Two extra faces the result screen wants, loaded straight from the
    // system font folder by file name (more robust than a registry lookup for
    // bold faces). Both are optional: whatever is missing falls back to the
    // body font.
    auto addResultFonts = [&]() {
#ifdef _WIN32
        wchar_t windowsDir[MAX_PATH]{};
        GetWindowsDirectoryW(windowsDir, MAX_PATH);
        const std::string fontRoot = wideToUtf8(windowsDir) + "\\Fonts\\";
        auto openFont = [&](const char* fileName, ImFont*& outFont, const char* label,
                            const ImWchar* ranges) {
            if (outFont != nullptr) {
                return;
            }
            const std::string path = fontRoot + fileName;
            std::FILE* probe = std::fopen(path.c_str(), "rb");
            if (probe == nullptr) {
                return;
            }
            std::fclose(probe);
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 2;
            std::snprintf(config.Name, sizeof(config.Name), "%s", fileName);
            outFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 42.0f, &config, ranges);
            if (outFont != nullptr) {
                std::printf("[intro] result %s face %s loaded\n", label, fileName);
            }
        };
        // Heavy CJK (得分 / 最高得分 / 歌曲等级 / 继续).
        if (gBodyFont == nullptr) {
            gBodyFont = io.Fonts->AddFontDefault();
        }
        for (const char* file : {"msyhbd.ttc", "Dengb.ttf", "simhei.ttf", "msjhbd.ttc"}) {
            openFont(file, gBoldFont, "bold", io.Fonts->GetGlyphRangesJapanese());
            if (gBoldFont == nullptr) {
                continue;
            }
            // Merge the simplified-Chinese set for 纪 / 录 / 级 / 继 / 续.
            ImFontConfig merge;
            merge.MergeMode = true;
            merge.OversampleH = 2;
            merge.OversampleV = 2;
            io.Fonts->AddFontFromFileTTF((fontRoot + file).c_str(), 42.0f, &merge,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            break;
        }
        // Condensed bold latin (PERFECT/GREAT/.../SCORERANK/RESULT).
        for (const char* file : {"ARIALNB.TTF", "ARIALN.TTF"}) {
            openFont(file, gCondFont, "condensed", nullptr);
            if (gCondFont != nullptr) {
                break;
            }
        }
#endif
        if (gBoldFont == nullptr) {
            gBoldFont = gBodyFont;
        }
        if (gCondFont == nullptr) {
            gCondFont = gBodyFont;
        }
    };

#ifdef _WIN32
    // The system UI font, and nothing else. 2026-09-19: assets/mmw/font was
    // removed - the FOT-Rodin pair is a Fontworks *commercial* face (the whole
    // embedding question in CREDITS.md / COPYRIGHT.md is gone with it) and the
    // Noto fallback was 16.9 MB for the few hundred glyphs this UI draws. A
    // candidate is only accepted once it proves it can actually render CJK - a
    // latin-only face (Segoe UI on a Japanese or Chinese desktop, for instance)
    // would turn every title into tofu.
    {
        // Returns the first probe glyph the face cannot draw, or 0 when the face
        // passes. The set deliberately mixes Japanese and Simplified Chinese:
        // 初 ミ 詞 are ja (kana + shinjitai) and 设 is the SC-only form of 設 -
        // so a Japanese-only face (Meiryo, MS Gothic) fails on 设 and a
        // Chinese-only face (most 方正/汉仪 faces) fails on ミ. One file has to
        // cover both: the song titles are Japanese and this UI's own strings
        // (谱面加载中… / 得分 / 继续) are Simplified Chinese.
        auto missingCjkGlyph = [](ImFont* font, float size) -> ImWchar {
            ImFontBaked* baked = font->GetFontBaked(size);
            if (baked == nullptr) {
                return ImWchar(0xFFFF); // atlas never built - not a coverage problem
            }
            for (const ImWchar c : {ImWchar(0x521D), ImWchar(0x30DF), ImWchar(0x8A5E), ImWchar(0x8BBE)}) {
                if (baked->FindGlyphNoFallback(c) == nullptr) {
                    return c;
                }
            }
            return 0;
        };

        // Logger for the "which font did it pick" question: on a box where every
        // candidate fails the UI silently ends up on ImGui's built-in bitmap face
        // (dot matrix, every CJK glyph a "?"), and the only way to see why is the
        // candidate list plus the reason each one was dropped.
        std::vector<SystemFontCandidate> candidates;
        // CPSEKAI_FONT_FILE=<path>: use exactly this face. Diagnostics, and a
        // way out on a machine whose fonts we fail to recognise automatically.
        if (const char* forced = std::getenv("CPSEKAI_FONT_FILE"); forced != nullptr && *forced != '\0') {
            candidates.push_back({forced, forced});
        }
        for (const auto& candidate : systemFontCandidates()) {
            candidates.push_back(candidate);
        }
        std::printf("[intro] %d system font candidate(s)\n", static_cast<int>(candidates.size()));
        for (const auto& candidate : candidates) {
            std::printf("[intro]   candidate %s -> %s\n", candidate.face.c_str(), candidate.path.c_str());
        }

        for (const auto& candidate : candidates) {
            // AddFontFromFileTTF only says "nullptr" for both "cannot open" and
            // "cannot parse", and on Win7 those two have completely different
            // causes. Tell them apart in the log.
            if (std::FILE* probe = std::fopen(candidate.path.c_str(), "rb"); probe == nullptr) {
                std::printf("[intro] system font %s not readable (%s)\n", candidate.face.c_str(),
                    candidate.path.c_str());
                continue;
            } else {
                std::fclose(probe);
            }
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 2;
            std::snprintf(config.Name, sizeof(config.Name), "%s", candidate.face.c_str());
            // Japanese first: it carries all the kanji song titles need.
            ImFont* font = io.Fonts->AddFontFromFileTTF(candidate.path.c_str(), 42.0f, &config,
                io.Fonts->GetGlyphRangesJapanese());
            if (font == nullptr) {
                std::printf("[intro] system font %s rejected by the rasterizer\n", candidate.path.c_str());
                continue;
            }
            const ImWchar missing = missingCjkGlyph(font, 42.0f);
            if (missing != 0) {
                std::printf("[intro] system font %s lacks U+%04X, trying the next one\n",
                    candidate.face.c_str(), static_cast<unsigned>(missing));
                continue;
            }
            // Merge the simplified-Chinese set the built-in UI draws.
            ImFontConfig merge;
            merge.MergeMode = true;
            merge.OversampleH = 2;
            merge.OversampleV = 2;
            io.Fonts->AddFontFromFileTTF(candidate.path.c_str(), 42.0f, &merge,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            gBodyFont = font;
            gTitleFont = font;
            gDiffFont = font;
            addResultFonts();
            io.Fonts->Build();
            std::printf("[intro] system font %s @42px loaded (%s)\n", candidate.face.c_str(),
                candidate.path.c_str());
            return;
        }
        std::printf("[intro] no usable system font, falling back to ImGui's built-in face\n");
    }
#endif

    // Nothing bundled to fall back to any more: whatever the system handed us is
    // what the UI uses, and ImGui's own bitmap face is the last resort (it draws
    // latin only, but keeping the process alive with readable numbers beats
    // starting with no font at all).
    if (gBodyFont == nullptr) {
        gBodyFont = io.Fonts->AddFontDefault();
    }
    if (gTitleFont == nullptr) {
        gTitleFont = gBodyFont;
    }
    if (gDiffFont == nullptr) {
        gDiffFont = gTitleFont;
    }
    addResultFonts();
    io.Fonts->Build();
}

ImFont* titleFont()
{
    return gTitleFont;
}

ImFont* bodyFont()
{
    return gBodyFont;
}

ImFont* difficultyFont()
{
    return gDiffFont;
}

ImFont* boldFont()
{
    return gBoldFont;
}

ImFont* condensedFont()
{
    return gCondFont;
}

IntroInfo buildIntroInfo(const IntroMetadata& metadata, bool hasCover)
{
    IntroInfo intro;
    intro.hasCover = hasCover;
    intro.title = trimText(metadata.title);
    if (intro.title.empty()) {
        intro.title = "Unknown Title";
    }

    const std::string lyricist = trimText(metadata.lyricist).empty() ? std::string("-") : trimText(metadata.lyricist);
    std::string composer = trimText(metadata.composer);
    if (composer.empty()) {
        composer = "-";
    }
    const std::string arranger = trimText(metadata.arranger).empty() ? std::string("-") : trimText(metadata.arranger);
    const std::string vocal = trimText(metadata.vocal).empty() ? std::string("-") : trimText(metadata.vocal);

    intro.description1 = "作词：" + lyricist + "　作曲：" + composer + "　编曲：" + arranger;
    intro.description2 = "Vo. " + vocal;
    intro.difficulty = normalizeDifficulty(trimText(metadata.difficulty));
    if (intro.difficulty.empty()) {
        intro.difficulty = inferDifficultyFromSusPath(metadata.susPath);
    }

    intro.hasContent = intro.hasCover || !intro.title.empty() || !intro.description1.empty()
        || !intro.description2.empty() || !intro.difficulty.empty();
    return intro;
}

float introCardAlpha(float outputTimeSec, bool hasIntroContent)
{
    if (!hasIntroContent || outputTimeSec < 0.0f) {
        return 0.0f;
    }
    const float fadeOutStartSec = std::max(0.0f, kHudIntroDurationSec - INTRO_EXIT_FADE_SEC);
    if (outputTimeSec < fadeOutStartSec) {
        return 1.0f;
    }
    if (outputTimeSec < kHudIntroDurationSec) {
        return clamp01(1.0f - (outputTimeSec - fadeOutStartSec) / std::max(0.001f, INTRO_EXIT_FADE_SEC));
    }
    return 0.0f;
}

float openingPlayfieldVisibility(float outputTimeSec, bool hasIntroContent)
{
    if (!hasIntroContent || outputTimeSec < 0.0f) {
        return 1.0f;
    }
    const float revealStartSec = kHudIntroDurationSec + kIntroCleanBgDurationSec;
    if (outputTimeSec < revealStartSec) {
        return 0.0f;
    }
    return clamp01((outputTimeSec - revealStartSec) / kIntroPlayfieldFadeInSec);
}

void drawIntro(platform::Renderer& renderer, const IntroInfo& intro, float outputTimeSec,
    int windowW, int windowH)
{
    const float cardAlpha = introCardAlpha(outputTimeSec, intro.hasContent);
    const float maskAlpha =
        (intro.hasContent && outputTimeSec >= 0.0f && outputTimeSec < kHudIntroDurationSec)
            ? INTRO_BG_ALPHA
            : 0.0f;
    if (cardAlpha <= 0.001f && maskAlpha <= 0.001f) {
        return;
    }

    ImDrawList* overlay = ImGui::GetForegroundDrawList();
    const float scale = std::min(static_cast<float>(windowW) / 1920.0f, static_cast<float>(windowH) / 1080.0f);
    const float offsetX = (static_cast<float>(windowW) - 1920.0f * scale) * 0.5f;
    const float offsetY = (static_cast<float>(windowH) - 1080.0f * scale) * 0.5f;
    auto px = [&](float x) { return offsetX + x * scale; };
    auto py = [&](float y) { return offsetY + y * scale; };
    auto ps = [&](float v) { return v * scale; };

    auto drawHudImage = [&](const platform::Renderer::HudSprite* sprite, float x, float y, float width, float height,
                            float alpha) {
        if (sprite == nullptr || sprite->id == 0 || width <= 0.1f || height <= 0.1f) {
            return;
        }
        overlay->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
            ImVec2(x, y), ImVec2(x + width, y + height),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, alphaByte(alpha)));
    };

    // Full-screen purple mask; the stage and notes are hidden underneath.
    if (maskAlpha > 0.001f) {
        overlay->AddRectFilled(
            ImVec2(px(0.0f), py(0.0f)),
            ImVec2(px(1920.0f), py(1080.0f)),
            IM_COL32(104, 104, 156, alphaByte(maskAlpha)));
    }

    // Two rising gradient waves (overlay/start_grad.png).
    const platform::Renderer::HudSprite* gradTexture = renderer.hud("start_grad");
    if (maskAlpha > 0.001f && gradTexture != nullptr && gradTexture->id != 0) {
        const float introTimeSec = std::min(
            kHudIntroDurationSec + INTRO_GRAD_DURATION_SEC * 0.5f,
            std::max(0.0f, outputTimeSec + INTRO_GRAD_DURATION_SEC * 0.5f));
        for (int waveIndex = 0; waveIndex < 2; ++waveIndex) {
            const float waveStartSec = INTRO_GRAD_START_SEC + static_cast<float>(waveIndex) * INTRO_GRAD_DURATION_SEC;
            const float normalized = (introTimeSec - waveStartSec) / INTRO_GRAD_DURATION_SEC;
            if (normalized <= 0.0f || normalized >= 1.0f) {
                continue;
            }
            const float eased = easeOutQuad(normalized);
            const float offsetYValue = INTRO_GRAD_START_Y + (INTRO_GRAD_END_Y - INTRO_GRAD_START_Y) * eased;
            const float drawX = (1920.0f - INTRO_GRAD_DRAW_WIDTH) * 0.5f;
            const float drawY = (1080.0f - INTRO_GRAD_DRAW_HEIGHT) * 0.5f + offsetYValue;
            drawHudImage(gradTexture, px(drawX), py(drawY),
                ps(INTRO_GRAD_DRAW_WIDTH), ps(INTRO_GRAD_DRAW_HEIGHT), INTRO_GRAD_ALPHA);
        }
    }

    // Jacket + difficulty badge.
    if (cardAlpha > 0.001f && intro.hasCover) {
        const platform::Renderer::HudSprite* coverTexture = renderer.cover();
        if (coverTexture != nullptr && coverTexture->id != 0) {
            const float coverTop = 1080.0f - INTRO_COVER_BOTTOM_PX - INTRO_COVER_SIZE_PX;
            if (!intro.difficulty.empty()) {
                const std::string upper = toUpper(intro.difficulty);
                const int diffAlpha = alphaByte(cardAlpha);
                auto difficultyColor = [&](const std::string& upperDifficulty) -> ImU32 {
                    if (upperDifficulty == "EASY") {
                        return IM_COL32(75, 207, 138, diffAlpha);
                    }
                    if (upperDifficulty == "NORMAL") {
                        return IM_COL32(90, 140, 255, diffAlpha);
                    }
                    if (upperDifficulty == "HARD") {
                        return IM_COL32(242, 150, 77, diffAlpha);
                    }
                    if (upperDifficulty == "EXPERT") {
                        return IM_COL32(239, 90, 102, diffAlpha);
                    }
                    if (upperDifficulty == "MASTER") {
                        return IM_COL32(181, 91, 255, diffAlpha);
                    }
                    if (upperDifficulty == "APPEND") {
                        return IM_COL32(179, 162, 255, diffAlpha);
                    }
                    if (upperDifficulty == "ETERNAL") {
                        return IM_COL32(241, 192, 79, diffAlpha);
                    }
                    return IM_COL32(169, 56, 255, diffAlpha);
                };
                const float diffX = INTRO_COVER_LEFT_PX - 40.0f;
                const float diffTop = coverTop + 36.0f;
                const float diffSize = INTRO_COVER_SIZE_PX;
                if (upper == "APPEND") {
                    constexpr int appendStartR = 0xAD;
                    constexpr int appendStartG = 0x9F;
                    constexpr int appendStartB = 0xF6;
                    constexpr int appendEndR = 0xEF;
                    constexpr int appendEndG = 0x8D;
                    constexpr int appendEndB = 0xDA;
                    constexpr int appendMidR = (appendStartR + appendEndR) / 2;
                    constexpr int appendMidG = (appendStartG + appendEndG) / 2;
                    constexpr int appendMidB = (appendStartB + appendEndB) / 2;
                    overlay->AddRectFilledMultiColor(
                        ImVec2(px(diffX), py(diffTop)),
                        ImVec2(px(diffX + diffSize), py(diffTop + diffSize)),
                        IM_COL32(appendStartR, appendStartG, appendStartB, diffAlpha),
                        IM_COL32(appendMidR, appendMidG, appendMidB, diffAlpha),
                        IM_COL32(appendEndR, appendEndG, appendEndB, diffAlpha),
                        IM_COL32(appendMidR, appendMidG, appendMidB, diffAlpha));
                } else {
                    overlay->AddRectFilled(
                        ImVec2(px(diffX), py(diffTop)),
                        ImVec2(px(diffX + diffSize), py(diffTop + diffSize)),
                        difficultyColor(upper));
                }
                overlay->AddText(
                    gDiffFont,
                    ps(INTRO_DIFF_DRAW_SIZE_PX),
                    ImVec2(px(diffX + 10.0f), py(diffTop + diffSize - 42.0f + INTRO_DIFF_LABEL_Y_OFFSET_PX)),
                    IM_COL32(247, 250, 255, diffAlpha),
                    intro.difficulty.c_str());
            }
            drawHudImage(coverTexture, px(INTRO_COVER_LEFT_PX), py(coverTop),
                ps(INTRO_COVER_SIZE_PX), ps(INTRO_COVER_SIZE_PX), cardAlpha);
        }
    }

    const float textLeft = intro.hasCover ? INTRO_TEXT_LEFT_WITH_COVER_PX : INTRO_TEXT_LEFT_NO_COVER_PX;
    const float blockTop = 1080.0f - INTRO_TEXT_BOTTOM_PX - 180.0f + INTRO_TEXT_BLOCK_SHIFT_Y_PX;
    const float textMaxWidth = std::max(320.0f, 1920.0f - textLeft - 120.0f);
    const ImU32 titleColor = IM_COL32(246, 251, 255, alphaByte(cardAlpha));
    const ImU32 metaColor = IM_COL32(255, 255, 255, alphaByte(cardAlpha));
    if (cardAlpha > 0.001f) {
        drawSpacedUtf8Text(
            overlay,
            gTitleFont,
            ps(INTRO_TITLE_DRAW_SIZE_PX),
            ImVec2(px(textLeft), py(blockTop)),
            titleColor,
            intro.title,
            ps(INTRO_TITLE_LETTER_SPACING_PX),
            ps(textMaxWidth));
        overlay->AddText(
            gBodyFont,
            ps(INTRO_BODY_DRAW_SIZE_PX),
            ImVec2(px(textLeft), py(blockTop + INTRO_DESC1_ROW_OFFSET_PX)),
            metaColor,
            intro.description1.c_str(),
            nullptr,
            ps(textMaxWidth));
        overlay->AddText(
            gBodyFont,
            ps(INTRO_BODY_DRAW_SIZE_PX),
            ImVec2(px(textLeft), py(blockTop + INTRO_DESC2_ROW_OFFSET_PX)),
            metaColor,
            intro.description2.c_str(),
            nullptr,
            ps(textMaxWidth));
    }

    // Skip button, bottom-right: a quiet translucent pill with 跳过 >>.
    // Draw-only here - the click lands in main.cpp via introSkipHitTest().
    const float skipFade = clamp01((kHudIntroDurationSec - outputTimeSec) / 0.5f);
    if (outputTimeSec >= 0.0f && skipFade > 0.001f) {
        constexpr float kSkipW = 128.0f;
        constexpr float kSkipH = 46.0f;
        constexpr float kSkipMargin = 40.0f;
        const float bx = px(1920.0f - kSkipMargin - kSkipW);
        const float by = py(1080.0f - kSkipMargin - kSkipH);
        overlay->AddRectFilled(ImVec2(bx, by), ImVec2(bx + ps(kSkipW), by + ps(kSkipH)),
            IM_COL32(255, 255, 255, alphaByte(0.14f * skipFade)), ps(0.5f * kSkipH));
        overlay->AddRect(ImVec2(bx, by), ImVec2(bx + ps(kSkipW), by + ps(kSkipH)),
            IM_COL32(255, 255, 255, alphaByte(0.42f * skipFade)), ps(0.5f * kSkipH));
        const char* label = "跳过 >>";
        const float labelSize = ps(22.0f);
        const ImVec2 labelSize2 = gBodyFont->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, label);
        overlay->AddText(gBodyFont, labelSize,
            ImVec2(bx + 0.5f * (ps(kSkipW) - labelSize2.x), by + 0.5f * (ps(kSkipH) - labelSize2.y)),
            IM_COL32(255, 255, 255, alphaByte(0.85f * skipFade)), label);
    }

    (void)INTRO_ENTER_FADE_SEC;
}

bool introSkipHitTest(int windowW, int windowH, int x, int y)
{
    // Same transform + rect constants drawIntro() uses for the pill.
    constexpr float kSkipW = 128.0f;
    constexpr float kSkipH = 46.0f;
    constexpr float kSkipMargin = 40.0f;
    const float scale = std::min(static_cast<float>(windowW) / 1920.0f, static_cast<float>(windowH) / 1080.0f);
    const float offsetX = (static_cast<float>(windowW) - 1920.0f * scale) * 0.5f;
    const float offsetY = (static_cast<float>(windowH) - 1080.0f * scale) * 0.5f;
    const float bx = offsetX + (1920.0f - kSkipMargin - kSkipW) * scale;
    const float by = offsetY + (1080.0f - kSkipMargin - kSkipH) * scale;
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    return fx >= bx && fx <= bx + kSkipW * scale && fy >= by && fy <= by + kSkipH * scale;
}

} // namespace game
