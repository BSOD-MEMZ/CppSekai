// CppSekai - romaji -> kana, for the two search boxes.
//
// Someone without a Japanese IME types "gurume" and still finds 「いますぐ輪廻」:
// the query is folded to kana and matched against the official reading
// (musics.json's `pronunciation`), which is what both programs already search.
// Header-only and dependency-free, so chartdl - which shares no other code with
// the game - can use it too.
//
// The conversion is the usual greedy longest-match against a table. There is no
// attempt at real transliteration rules: the point is that a player types the
// reading the way it sounds ("imasugurinne" -> いますぐりんね), so the table
// only has to cover the kana that appear in song readings.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>

namespace romaji
{
namespace detail
{
    struct Entry
    {
        const char* romaji;
        const char* kana;
    };

    // Ordered longest-first, so the scan can take the first match: "kya" before
    // "ka" before "k". Within one length the order only matters for variants
    // that produce different kana (si/shi, tu/tsu).
    inline const Entry* lookupTable(std::size_t& count)
    {
        static const Entry entries[] = {
            // 3 characters
            {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
            {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
            {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
            {"sya", "しゃ"}, {"syu", "しゅ"}, {"syo", "しょ"},
            {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
            {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"},
            {"cya", "ちゃ"}, {"cyu", "ちゅ"}, {"cyo", "ちょ"},
            {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
            {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
            {"bya", "びゃ"}, {"byu", "びゅ"}, {"byo", "びょ"},
            {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
            {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
            {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
            {"ja", "じゃ"},  {"ju", "じゅ"},  {"jo", "じょ"},
            {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"},
            {"zya", "じゃ"}, {"zyu", "じゅ"}, {"zyo", "じょ"},
            {"dya", "ぢゃ"}, {"dyu", "ぢゅ"}, {"dyo", "ぢょ"},
            {"she", "しぇ"}, {"che", "ちぇ"}, {"je", "じぇ"},
            {"thi", "てぃ"}, {"dhi", "でぃ"},
            // 2 characters
            {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
            {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
            {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
            {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
            {"ha", "は"}, {"hi", "ひ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
            {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
            {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
            {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
            {"wa", "わ"}, {"wo", "を"},
            {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
            {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
            {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
            {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
            {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
            {"fa", "ふぁ"}, {"fi", "ふぃ"}, {"fe", "ふぇ"}, {"fo", "ふぉ"},
            {"va", "ヴぁ"}, {"vi", "ヴぃ"}, {"vu", "ヴ"},  {"ve", "ヴぇ"}, {"vo", "ヴぉ"},
            {"wi", "うぃ"}, {"we", "うぇ"},
            // Deliberately no "nn" entry. Before a vowel the second n belongs
            // to the next syllable - "rinne" is りんね, not りんえ, and "onna"
            // is おんな - so the single "n" rule below has to consume the first
            // one and let "ne"/"na" match next. "nn" at the end of a word or in
            // front of a consonant still comes out as one ん ("shinbun" =
            // しんぶん), which the same rule handles.
            // 1 character
            {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
            {"n", "ん"},
        };
        count = sizeof(entries) / sizeof(entries[0]);
        return entries;
    }
} // namespace detail

// Folds a latin query to kana. Characters the table does not know (kanji,
// kana the user typed directly, digits, spaces) are copied through, so feeding
// it an already-Japanese query is harmless.
inline std::string toKana(const std::string& input)
{
    std::string lowered;
    lowered.reserve(input.size());
    for (const char ch : input) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    std::size_t count = 0;
    const detail::Entry* entries = detail::lookupTable(count);
    const auto isVowel = [](char ch) {
        return ch == 'a' || ch == 'i' || ch == 'u' || ch == 'e' || ch == 'o';
    };

    std::string out;
    out.reserve(input.size() * 2);
    std::size_t i = 0;
    while (i < lowered.size()) {
        const char current = lowered[i];
        // Sokuon: a doubled consonant (not "n", and not a doubled vowel) means
        // the *next* syllable starts with a small っ - "gakkou" -> がっこう.
        if (i + 1 < lowered.size() && current == lowered[i + 1] && std::isalpha(static_cast<unsigned char>(current))
            && current != 'n' && !isVowel(current)) {
            out += "っ";
            ++i;
            continue;
        }
        if (current == '-') {
            out += "ー";
            ++i;
            continue;
        }
        bool matched = false;
        for (std::size_t e = 0; e < count; ++e) {
            const std::size_t length = std::strlen(entries[e].romaji);
            if (lowered.compare(i, length, entries[e].romaji) != 0) {
                continue;
            }
            out += entries[e].kana;
            i += length;
            matched = true;
            break;
        }
        if (!matched) {
            out.push_back(input[i]);
            ++i;
        }
    }
    return out;
}

} // namespace romaji
