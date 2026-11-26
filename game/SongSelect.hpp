// CppSekai - song select screen.
// Scans a folder for SUS charts and pairs each one with the BGM / jacket
// that sits next to it, then draws a pjsk-ish list + detail panel.
#pragma once

#include "platform/Renderer.hpp"

#include "imgui.h" // ImVec2 in drawSongSelect's out-parameter

#include <map>
#include <string>
#include <vector>

namespace game
{

class DecideBurst;

struct ChartEntry
{
    std::string susPath;
    std::string bgmPath;
    std::string coverPath;
    std::string title;
    std::string artist;
    std::string lyricist;         // from a <stem>.json sidecar (SUS has no field)
    std::string composer;         // sidecar composer, falls back to SUS artist
    std::string arranger;         // sidecar arranger, falls back to SUS designer
    std::string vocal;            // "初音ミク、KAITO" style list
    std::string difficulty;       // EASY..MASTER / APPEND / ETERNAL
    std::string level;
    std::string kana;             // official reading (musics.json), may be empty
    std::string mv;               // "2D" / "3D" MV tag from the sidecar (optional)
    std::string displayName; // fallback label when the chart has no #TITLE

    // Music id parsed from the file name ("0075_master.sus" -> 75). unipjsk
    // names every chart that way; 0 when the name has no leading number.
    int musicId = 0;

    // Seconds of silence at the head of the BGM (pjsk's fillerSec). Chart time
    // 0 sits after it, so playback starts from this position in the file.
    // Set from the sidecar JSON ("fillerSec" / "offset"), otherwise detected.
    double audioStartSec = 0.0;

    // Best result for this chart, loaded from userdata.json next to the exe.
    bool cleared = false;
    bool fullCombo = false;
    // 全部 PERFECT（FullCombo 且没有 GREAT/GOOD/BAD）。选曲的「絞り込み」用它判
    // 「未AP」；老存档没有这个字段，一律当 false（见 ScoreRecord）。
    bool allPerfect = false;
    // 在收藏夹里（settings.favoriteMusicIds，按 musicId 记；见 applyFavorites）。
    // 列表右键 / 长按可以切换，分组可以按它分「收藏 / 其他」。
    bool favorite = false;
    // Highest score reached on this chart, 0 when it was never cleared. The
    // song select turns it into the score-rank badge next to the song info.
    double bestScore = 0.0;
    // 原作的 musicCategories（music-categories.json）：bit0 = 3DMV、bit1 = 2DMV、
    // bit2 = 原曲MV。0 = 三种都没有 = 「MVなし」。老存档/曲表缺项时也是 0。
    unsigned mvMask = 0;
    // musics.json 的 isNewlyWrittenMusic（「書き下ろし楽曲」）。
    bool newlyWritten = false;
};

// Persisted play results, keyed by the chart's file name (e.g.
// "0075_master.sus"). Cleared = the song was played to the end;
// fullCombo = cleared with no MISS.
struct ScoreRecord
{
    bool cleared = false;
    bool fullCombo = false;
    // 全部 PERFECT。2026-11-23 加：选曲的「絞り込み > クリア状況 > 未AP」需要它。
    // 老存档没有这个键 -> false，也就是那些歌的 AP 记录会被当成"没 AP 过"。
    bool allPerfect = false;
    // Highest score reached, used by the result screen for 最高得分 / 新纪录!.
    // Kept alongside the flags in the same file, so old saves stay valid.
    double bestScore = 0.0;
};

// ---------------------------------------------------------------------------
// Account: a local, offline profile plus the pjsk player rank.
//
// Nothing here is sent anywhere - it is a player card for the UI. The name and
// the school/organisation are deliberately *not* on screen during normal play:
// the only thing that is always visible is the level chip (top right of the
// song select and on the result screen). The profile itself shows up on demand
// (clicking the chip) and in 设置 -> 账户.
struct AccountData
{
    std::string name; // 昵称
    std::string org;  // 学校 / 组织
    std::string note; // 个性签名
    // 头像 (2026-11-25): a player-supplied image shown in multiplayer. Stored
    // as a path relative to the user-data folder (e.g.
    // "profiles/default_avatar.png") so moving the game folder keeps it
    // working. Empty = no avatar (the party scoreboard falls back to the
    // player's initial). Unlike the old card-face picker this is the user's
    // own image, uploaded from disk in 设置 > 账户.
    std::string avatar;

    int rank = 1;
    // Exp banked towards the *next* rank, i.e. addPlayerExp() already rolled
    // the excess over. rank 1 / exp 0 is a fresh account.
    double exp = 0.0;
    int plays = 0;             // runs played to the end (no autoplay)
    double totalScore = 0.0;   // sum of those runs' scores
};

// ---- Player rank ----------------------------------------------------------
// Curve taken from the official game (Sekaipedia "Player Rank"). There, a live
// grants `m_score * m_bonus`, and the exp a rank needs grows in bands. We have
// no live-bonus system, so a run grants exactly the score-rank multiplier and
// the curve is used as-is.
//
// Exp needed to go from `rank` to `rank + 1`. Bands: rank 1 is the tutorial
// hand-out, then a flat 8010, +500 per rank up to 12, +1000 up to 15 and +480
// from there on. Above kMaxPlayerRank the cap holds (0 = no next rank).
constexpr int kMaxPlayerRank = 900;
double expToNextRank(int rank);

// Score rank ('d' / 'c' / 'b' / 'a' / 's', as produced by
// game::scoreRankAndBar) -> the exp an official live would grant for it.
int scoreRankExp(char rank);

// 演出能量 -> 结算经验的倍率。
//
// 官方（4.0.0 平衡调整之后）的表：
//   0 → ×1    1 → ×5    2 → ×10   3 → ×15   4 → ×20   5 → ×25
//   6 → ×26   7 → ×27   8 → ×28   9 → ×29  10 → ×30
// 能消耗的档位到 10（平时上限 5、活动期间放到 10），再往上官方没有数据。
// 6 档起就是「20 + 消耗」这条直线，11 档以上本项目**照这条线外推** ——
// 这段是我们自己的延伸，不是官方数值（设置页那一行和 AGENTS.md 都写明了）。
double liveBonusExpMultiplier(int bonus);

// Banks `amount` exp and rolls the rank over as often as it needs to.
// Returns the number of ranks gained (0 = none).
int addPlayerExp(AccountData& account, double amount);

// Settings + play results are persisted together in one userdata.json; see
// UserSettings / loadUserData below.

// Everything the player keeps between sessions. Written next to the charts so
// it survives a rebuilt build/ directory and travels with them: the score keys
// are chart *file names*, so the same charts re-downloaded anywhere match.
struct UserSettings
{
    float noteSpeed = 8.0f;
    float seVolume = 0.8f;   // hit / UI sound effects
    float bgmVolume = 1.0f;  // music: chart track, select preview, result BGM
    // Game controller vibration (0 = off). Scales the song-start kick, the
    // result screen's score roll, the 确定 flash and the per-hit flavours
    // below; a pad without motors just ignores it.
    float padRumble = 0.7f;
    // Which moments buzz (settings > 判定). Each one is a short kick while the
    // pad is already busy with the two above; all of them are moot while
    // padRumble is 0.
    bool rumbleFlick = true;    // a flick note was cleared
    bool rumbleCritical = true; // a 绝赞 (yellow / critical) note was cleared
    bool rumbleMiss = false;    // a note was missed / life was lost
    double offsetSec = 0.0; // audio offset; the UI shows it in ms
    double leadInSec = 6.0;
    int windowMode = 1; // 0=borderless 1=windowed 2=fullscreen
    // Windowed/borderless resolution. Also the *render* resolution when
    // renderScale is 1 (then it is what the game draws at, whatever the window
    // size is). Fullscreen always uses the desktop size for the window, but in
    // fixed mode the render size is still this.
    int windowWidth = 1366;
    int windowHeight = 768;
    // Where the window was last time (the other half of 分辨率, which only ever
    // stored the size). -1 = 没有意见, put it in the middle of the primary
    // screen. A remembered spot that no longer lands on any attached display
    // (the second monitor it lived on is gone) is ignored the same way, so a
    // laptop that undocks never opens a window off-screen - see main.cpp.
    // Recorded on every save (the position as of the last settings write or
    // exit), so it costs nothing while the game runs.
    int windowX = -1;
    int windowY = -1;
    int fpsLimit = 60;  // 0 = vsync only; >refresh rate auto-disables vsync
    // Render size. 0 = the window size *is* the render size (dragging the
    // window relayouts the lanes and the HUD), 1 = always render at
    // windowWidth x windowHeight and scale that picture into the window with
    // the aspect ratio kept (letterbox) - dragging then only zooms the picture,
    // it never changes the layout.
    int renderScale = 0;
    // How many copies of the game may run at once.
    //   0 = one only: launching a second one just brings the running window to
    //       the front and exits (two copies would fight over the same save).
    //   1 = several: every extra instance logs in as a *different* user, so no
    //       two windows ever write the same profile file. Costs one full copy
    //       of the assets and textures per window (~250 MB); see AGENTS.md.
    int instanceMode = 0;
    // The user has been told that multi-open is experimental and said yes. The
    // confirmation is asked once, the first time 允许多开 (or 多人游玩, which
    // implies it) is turned on; afterwards the switch flips silently. Stored
    // per profile so a fresh user gets asked again.
    bool multiInstanceAccepted = false;
    // Multi-instance co-op ("多人游玩"). On: this window joins the shared room
    // every other window on this machine is in (see platform/Party.hpp), and
    // the instance policy allows several copies at once. The first window that
    // comes up becomes the host: it picks the song, every window picks its own
    // difficulty and they all start on the same beat, with the host alone
    // playing the BGM. Off (default) = plain single-window play.
    bool multiplayer = false;
    // Subtle playback progress bar along the top edge of the play screen.
    bool showProgressBar = true;
    // Hide the Windows touch ripple over our window (per-window setting).
    bool hideTouchFeedback = true;
    // 弱化打击特效 (weaken hit effects): keep only the most basic layer of a hit
    // burst - the judgement spark (*_gen) and a flick's flash (*_flash) - and
    // skip the lane lights, the auras drawn under the notes, and the particles
    // a long note burns while it is held. Same idea as pjsk's light effect
    // profile, and it drops a large slice of the per-frame overdraw, which is
    // what a weak integrated GPU notices first. Default off (full effects).
    // Implementation: core/native/mmw_port/EffectView.cpp (isSuppressed).
    bool simpleEffects = false;
    // 左下角显示当前帧率 (smoothed). Off by default; the play screen only.
    bool showFps = false;
    // 每 lane 一个键位，存 SDL_Keycode 的数值（见 main.cpp 的 kLaneKeys 作默认）。
    // 空数组 = 用默认的 z s x d c v g b h n j m；长度为 12 时逐 lane 生效。
    std::vector<int> laneKeys;
    // 收藏夹里的曲目，按 musicId 记（0 表示这个谱面文件没有编号，不收）。
    // 列表分组可以按它分「收藏 / 其他」，右键曲目即可切换。
    std::vector<int> favoriteMusicIds;
    // 最近打过的谱面路径（最近在前，最多 10 条）。**只喂 Windows 的任务栏跳转列表**
    // （右键任务栏图标那份菜单），游戏自己不用它 —— 所以记在哪条起奏路径上都行，
    // 见 main.cpp 的 announceTrack。
    std::vector<std::string> recentSusPaths;
    // Judgement windows, in ms. These defaults are the 宽松 preset (the official
    // set opened up by 30 ms on every window); 设置 > 判定 > 判定预设 offers
    // 标准 (the bare official set) and 严格 (30 ms tighter) next to it.
    float perfectMs = 72.0f;
    float greatMs = 113.0f;
    float goodMs = 138.0f;
    // BAD window end, and the point where an untouched note auto-misses. With
    // `linkBadMiss` on (default) the two are the same number: BAD stops exactly
    // where the note gives up. Off lets them drift apart, so a press may still
    // read as BAD for a moment after the note has already missed on its own.
    float badMs = 155.0f;
    float missMs = 155.0f;
    bool linkBadMiss = true;
    // Long-note tolerance. Letting go earlier than holdTailGraceMs before a
    // hold's end breaks it; the lane may be grabbed up to holdStartGraceMs
    // after the hold start without counting as a break. Forgiving = the classic
    // 180 / 140, tight = 120 / 80, strict = 60 / 40.
    float holdTailGraceMs = 180.0f;
    float holdStartGraceMs = 140.0f;
    // Life a run starts with (100..5000). The HUD bar is normalised against it,
    // so whatever the value the bar reads 100% when the live starts; a bigger
    // pool just takes longer to drain. Pushed to the judgement engine, which
    // seeds its stats from it on every reset.
    float initialLife = 1000.0f;
    // 卡组综合力 (设置 > 判定 > 综合力). The official score model scales with the
    // deck's TotalPowerIncludeBuff (see MV2D-SCORE.md 2.1), and we have no deck
    // system - so the number is a plain setting. Score is linear in it: 250000
    // is roughly a full team of max-level 4-stars, which is what the old
    // hard-coded kTeamPower meant. 1000..500000.
    int deckPower = 250000;
    bool strictFlick = true;
    // 默认歌手偏好（设置 > 演奏 > 默认歌手，独立卡片）。
    //   0 = 跟随歌曲 —— 官方的セカイver，也就是一直以来的行为
    //   1 = 优先虚拟歌手 ver（type == "virtual_singer"）
    //   2 = 优先指定歌手（vocalPrefSinger，**日文原名**，和 music-vocals.json
    //       里 singers 数组逐字比对）
    // 只在"这首歌真有这个版本、而且音频在本地"时才生效，否则回落到 0 ——
    // 所以设了偏好也不会让一首没下过另一个版本的歌变成放不出声。
    int vocalPref = 0;
    std::string vocalPrefSinger;
    // 日志分类开关（设置 > 系统 > 日志输出）。位掩码，见 platform/Log.hpp：
    // bit0 系统 / 1 选曲 / 2 剧情 / 3 演奏 / 4 音频 / 5 输入 / 6 渲染 / 7 多人。
    // 默认 0xFF（全开 = 以前的行为）。关掉的类别连格式化都不做。
    unsigned logMask = 0xFFu;
    // "Flick 视作 Tap" (settings > 判定): flick notes take any press instead of
    // a directional swipe, for touchscreens that can barely send an upward
    // swipe. A hold's flick TAIL is not converted - it stops being a note (see
    // game/Judgement.hpp).
    bool flickAsTap = false;
    // Flick debug log (settings > 判定 > Flick 调试日志): while on, every raw
    // touch sample, the swipe measurement the classifier saw and the judgement
    // it produced go to flick_debug.log next to the exe, so a missed touch
    // flick can be diagnosed after the fact.
    bool debugLog = false;
    // Autoplay chart preview (all-PERFECT run, AUTO judge text, no records).
    bool autoplay = false;
    // Pause the run when the window loses keyboard focus (alt-tab, a popup
    // stealing focus). Off = the song keeps running in the background.
    bool autoPauseOnBlur = true;
    // 最小化窗口时静音 (settings > 系统). Global: it silences the *engine*, so
    // the song-select preview and the result BGM go quiet too, not just the
    // chart track. Restoring the window lifts it again. Independent of
    // autoPauseOnBlur: that one only exists on the play screen, this one is for
    // "I alt-tabbed away and do not want the game talking to the room".
    bool muteWhenMinimized = false;
    // 开始游戏时切换到英文输入法 (settings > 演奏). On starting a live, ask the
    // active IME for its alphanumeric mode, so the 12 lane keys reach the game
    // instead of being swallowed by a pinyin composition window. Only touches
    // the *mode* of whatever IME is already selected - it never changes the
    // player's keyboard layout. See switchImeToAscii() in main.cpp.
    bool switchImeOnStart = false;
    // Report the current song to Windows (SMTC: the volume flyout / taskbar
    // media widget). Off = nothing is announced, so a media widget showing
    // something else is left alone.
    bool reportSmtc = true;
    // Splash style: 0 = static centered image (assets\splashscreen.png,
    // default), 1 = classic dark screen with title + progress bar.
    int splashStyle = 0;
    // Song-select background: 0 = the built-in gradient, 1 = the user's
    // Windows desktop wallpaper (blurred) so the screen matches the desktop.
    // 1 is the default: a fresh install comes up with the desktop's own
    // picture behind the list (a missing wallpaper falls back to the gradient).
    int bgStyle = 1;
    float bgBlur = 0.5f; // 0..1 blur amount for the wallpaper
    float bgDim = 0.45f; // 0..1 darkening on top of it (keeps the list readable)
    // 演出背景 (设置 > 演出 > MV). Mirrors LiveSettingData.LiveModeType, minus the
    // 3D tiers we cannot render:
    //   0 = 无 MV    - the current look: jacket composited onto the stage screens
    //   1 = 原曲 MV  - the "original MV" stage; needs a 2D MV for that song,
    //                  otherwise it falls back to the jacket (that is exactly what
    //                  the official game does when a song has no 2D MV)
    //   2 = 2D MV    - play the song's .usm on the background quad
    // Note 1 and 2 currently render the same thing; 1 only differs once a song
    // without a 2D MV exists in the library, and it then keeps the jacket.
    int mvMode = 0;
    // How "透明（Aero 玻璃）" (bgStyle 2) is put together; only read then, but
    // stored always so the answer survives switching the background away:
    //   0 = DwmExtendFrameIntoClientArea(-1) only. The client area is glass, but
    //       the caption and the frame DWM draws around it stay - on Win7 the
    //       window still looks like a sheet of glass *inside a window frame*.
    //   1 = 0 + DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED, i.e. ask DWM to stop
    //       drawing the non-client area. Undocumented territory: it may also take
    //       the glass (which comes from the frame) with it. Experiment.
    //   2 = remove the non-client area ourselves (WM_NCCALCSIZE -> 0) and put
    //       moving/resizing back by hand (WM_NCHITTEST): the recipe Microsoft
    //       documents in "Custom Window Frame Using DWM". Experiment.
    int glassMode = 0;
    // 原生菜单栏 (settings > 系统): put a real Win32 menu bar on the window and
    // move the three *window* commands off the song select's header row into it
    // (刷新谱面列表 / 音乐商店 / 设置). Off by default - the game's own chrome is
    // the normal look, and the in-game buttons are the ones players know.
    // Only a plain framed window can carry a menu bar (it is drawn in the
    // non-client area), so borderless / fullscreen / 玻璃实现 = 自绘无框 leave it
    // off while the setting stays on - see platform/NativeMenu.hpp.
    bool nativeMenuBar = false;
    // UI scale for the two screens laid out on a virtual canvas - song select and
    // the result screen (1.0 = fit the window). The play screen and the HUD
    // deliberately ignore it: those are played, not read, and a mis-scaled lane
    // would be worse than a small one.
    float uiScale = 1.0f;
    // Song-list order / grouping, so the list comes back the way it was left
    // (see game::drawSongSelect, which owns the two values while it runs).
    int sortMode = 0;  // 0 = by name, 1 = by difficulty
    // 0 = off, 1 = by level band, 2 = by reading (aiueo row: あ か さ た ...,
    // latin letters one section each), 3 = by first character (one section per
    // kana).
    int groupMode = 0;
    // 左侧团体页签（原作的 MusicSelectUnitTabList）。数值就是原作的
    // CategoryTabType：0 全部 / 1 バーチャル・シンガー / 2 Leo/need /
    // 3 MORE MORE JUMP! / 4 Vivid BAD SQUAD / 5 Wonderlands×Showtime /
    // 6 25時、ナイトコードで。/ 7 その他。
    int unitTab = 0;
    // 筛选弹窗（原作的 MusicSelectFilterDialog）。三个值就是原作的枚举，索引 = 枚举值：
    //   clearFilter = MusicSelectClearFilterType
    //       0 すべて / 1 未クリア / 2 未フルコンボ / 3 未スコアS / 4 未AP
    //   mvUnchecked = 4 个 MusicSelectMVFilterType 的「未勾选」位掩码
    //       bit0 3DMVあり / bit1 2DMVあり / bit2 原曲MVあり / bit3 MVなし
    //       （原作的 UnCheckedFilterMVTypes[] 就是这个意思：存"被排除"的类型）
    //   musicFilter = MusicSelectMusicFilterType
    //       0 すべて / 1 書き下ろし楽曲 / 2 APPENDあり / 3 クリエイト譜面あり
    int clearFilter = 0;
    int mvUnchecked = 0;
    int musicFilter = 0;
    // 演出能量 (演出 > 演出能量): how much ライブボーナス to stake on a live.
    // It multiplies the exp a run grants (see liveBonusExpMultiplier) - the same
    // thing it does in the official game, where a live grants score-rank exp
    // times the bonus multiplier. 0 = play without spending any. The official
    // ladder covers 0..10 (you can stake up to 5 normally, 10 during an event);
    // above that the curve is our own extension, spelled out on the function.
    int liveBonus = 5;
    // The 许可 / 免责声明 card (the ELUA) comes up over the song select on the
    // first run. Tick "以后不再显示" in it and this goes true, so the card never
    // appears again. It is a *picked* flag (the dialog does not have to be
    // dismissed to keep playing), so it lives per profile, like the settings it
    // is part of: a second user gets told once as well.
    bool eulaAccepted = false;
    // 新手教学看过没有（第一次启动自动播一遍；设置 > 关于 可以重播）。
    // 跳过 / 中途关掉也算看过 —— 反复弹一个玩家明确关掉的窗口是最烦人的事。
    // 和 eulaAccepted 一样按 profile 存，第二个玩家还会被教一次。
    bool tutorialDone = false;
};

// Path of userdata.json: <exeDir>\.. \userdata.json when a charts\ folder sits
// there (the usual build/ layout - so the file lands next to charts/ and
// survives wiping build/), otherwise <exeDir>\userdata.json for a packaged
// build. exeDir must end with a path separator.
std::string userDataPath(const std::string& exeDir);

// ---------------------------------------------------------------------------
// Local profiles (multi-user). Each user keeps their own settings / scores /
// account in <dataDir>\profiles\<id>.json; <dataDir>\profiles\index.json lists
// the users and which one is active. The first run copies the old single
// <dataDir>\userdata.json into the "default" profile, so nothing is lost.
// ---------------------------------------------------------------------------
struct UserProfile
{
    std::string id;   // file-name safe, unique
    std::string name; // what the UI shows
};

// Folder that holds userdata.json / profiles/ - i.e. dirname(userDataPath()).
std::string userDataDir(const std::string& exeDir);

// Reads the profile list, creating it (and importing userdata.json) when it is
// missing. `active` receives the id of the active profile and is never left
// empty.
std::vector<UserProfile> loadProfiles(const std::string& dataDir, std::string& active);

void saveProfiles(const std::string& dataDir, const std::vector<UserProfile>& profiles,
    const std::string& active);

// Path of one profile's data file.
std::string profileDataPath(const std::string& dataDir, const std::string& id);

// A free profile id derived from `name` ("初音" -> "user<hash>", "xxt" -> "xxt",
// a second "xxt" -> "xxt2").
std::string makeProfileId(const std::string& name, const std::vector<UserProfile>& existing);

// Reads settings + scores + account. A legacy flat scores.json (a bare map, no
// "settings"/"scores" wrapper) is still accepted, so an old file migrates.
// Missing keys keep the defaults already in `settings` / `account` - which is
// also what an existing userdata.json (written before the account existed)
// ends up as: defaults, no error.
void loadUserData(const std::string& path, UserSettings& settings,
    std::map<std::string, ScoreRecord>& scores, AccountData& account);

void saveUserData(const std::string& path, const UserSettings& settings,
    const std::map<std::string, ScoreRecord>& scores, const AccountData& account);

// True when `path` parses as a profile file - a JSON object carrying a
// "settings" or a "scores" member. 设置 > 账户 > 导入 asks this before its
// confirmation card, so a picked file that is *not* user data (any other json
// on the machine) is rejected while there is still nothing to undo.
bool isUserDataFile(const std::string& path);

// Records the result of one chart (merges with the existing record) and
// returns the merged record. `score` only ever raises the stored best.
// `allPerfect` is sticky like the other two flags (one AP run marks the chart).
ScoreRecord mergeScore(const ScoreRecord& old, bool cleared, bool fullCombo, bool allPerfect,
    double score = 0.0);

// One vocal version of a song (the official musicVocals table). `asset` is the
// game's assetbundleName, which is also the audio file stem next to the chart:
// charts\se_0374_01.mp3 etc. (see music-vocals.json / CHARTS.md).
struct VocalVersion
{
    int id = 0;
    std::string type;     // "sekai" / "virtual_singer" / "another_vocal" / ...
    std::string caption;  // "セカイver." - the official label
    std::string asset;    // assetbundleName, e.g. "an_0374_02"
    std::vector<std::string> singers;
    // The version's audio file exists next to the chart. Only filled in by
    // availableVocals(); the table itself does not know about files.
    bool available = false;
};

// Vocal versions of a song, in official order (empty when unknown).
const std::vector<VocalVersion>& musicVocals(int musicId);

// Same, but annotated with `available` - and only the versions that actually
// have their mp3 next to the chart. This is what the song select shows: no
// file means no chip, so a song with a single <id4>.mp3 download keeps looking
// exactly like before.
std::vector<VocalVersion> availableVocals(const ChartEntry& entry);

// Short label for a chip in the song select ("セカイ" / "バーチャル" / "みのり").
std::string vocalShortLabel(const VocalVersion& version);

// Index of the version to start with: the セカイver when there is one (that is
// also what the legacy <id4>.mp3 download is), else the first.
int defaultVocalIndex(int musicId, const std::vector<VocalVersion>& available);
// 设置 > 演奏 > 默认歌手：0 = 跟随歌曲（セカイver）, 1 = 虚拟歌手ver, 2 = 指定歌手
// （singer 是 music-vocals.json 里的日文原名）。改完设置推一次即可。
void setVocalPreference(int mode, const std::string& singer);

// Points `entry` at a version's audio (and its singers, for the "Vo." line).
// Returns false when that version has no file, in which case entry is left with
// whatever BGM the chart scan found.
bool applyVocalVersion(ChartEntry& entry, const std::vector<VocalVersion>& versions, int index);

// Absolute path of a version's mp3 (empty when it is not on disk). Used by the
// song select's music preview, which has to follow the chosen chip without
// mutating the (const) entry the list owns.
std::string vocalAudioPath(const ChartEntry& entry, int index);

// Gives `entry` a usable BGM when the plain chart scan found none: charts that
// ship only official vocal files (se_0374_01.mp3 / vs_0374_01.mp3 ...) have no
// <id4>.mp3 for findSidecar() to pick, and used to come out silent - including
// in the song select preview. Picks the セカイver like the version switcher's
// default. Does nothing when the entry already has a BGM.
void applyDefaultVocal(ChartEntry& entry);

// Official vocal table (music-vocals.json, generated by
// .workbuddy/tools/gen_music_vocals.py). Optional: without it the song select
// simply offers no version switcher.
void loadMusicVocals(const std::string& path);

// Community song aliases (music-aliases.json, exported from the public
// HarukiBot API by .workbuddy/tools/fetch_music_aliases.py). Optional like the
// other tables: without it the search just loses the alias branch ("tyw",
// "梦开始的地方", "mmj团歌" ...). Matching is exact, not substring.
void loadMusicAliases(const std::string& path);

// Per-song 2D MV badges, derived from what the player actually has on disk:
// charts/mv/<id4>.usm (original 2D MV) -> the blue 原曲 badge,
// charts/mv/<id4>_sekai.usm (セカイ 2D MV) -> the green 2D MV badge.
// NOT from the master categories: we do not render 3D MVs at all, so showing
// the master's yellow 3D badge would advertise a feature that does not exist -
// the badge set follows the downloaded files (user rule, 2026-11-15).
struct SongBadges
{
    bool original = false; // original 2D MV downloaded (blue frame)
    bool mv2d = false;     // セカイ 2D MV downloaded (green frame)
};
const SongBadges& songBadges(int musicId);

// Collaboration labels (music-collabs.json, generated from the official
// master table musicCollaborations). Optional: without it the phone panel
// simply shows no collaboration ribbon.
void loadMusicCollabs(const std::string& path);

// Collaboration label for a song ("劇場版プロジェクトセカイ", "ゲキ！チュウマイ
// コラボ" ...), from MasterMusic.musicCollaborationId -> MasterMusicCollaboration
// via musicCollaborations.json. Empty when the song has none.
std::string musicCollab(int musicId);

// Key used in scores.json: the chart's file name (e.g. "0075_master.sus").
std::string scoreKey(const ChartEntry& entry);

// Copies cleared / fullCombo from the map into the entries.
void applyScores(std::vector<ChartEntry>& entries, const std::map<std::string, ScoreRecord>& scores);

// 收藏夹：把 settings.favoriteMusicIds（musicId 列表）填进 entries[i].favorite。
// 收藏是按曲目记的（不是按难度），所以一个 group 的四个难度一起亮。
void applyFavorites(std::vector<ChartEntry>& entries, const std::vector<int>& favoriteMusicIds);

// Assets root (the directory that contains the "select" subfolder, i.e.
// <exeDir>\assets). The select UI PNGs (shuffle / settings buttons, the phone
// frame and the clear indicators) load from <dir>\select\<name>.png.
// Call once at startup.
void setSelectAssetDir(const std::string& dir);

// UI language for the select screen's *baked-text* assets. There are two
// folders: assets/select/zh-CN and assets/select/ja-JP. selectTex() tries
// <lang>/ first, then ja-JP/, then the language-neutral select/ root - so only
// the pictures that have words in them (the ALL / その他 tab logos, the
// "歌曲等级" / "楽曲Lv." plate) need a copy per language.
// "zh-CN" (the default, and what CPSEKAI_LANG picks when it is not "ja-JP") or
// "ja-JP"; anything else falls back to zh-CN. Safe to call more than once - the
// texture cache is keyed by language.
void setSelectLanguage(const std::string& lang);

// 右上角菜单抽屉（原作 ScreenLayerHeader 的 menuButton -> MenuDialog）是否
// 打开。main 的 ESC 链要在它打开时让位，否则 ESC 会被当成"退出游戏"。
bool menuDrawerOpen();
void closeMenuDrawer();
// `--select-menu` / CPSEKAI_SELECT_MENU=1 use this for a headless screenshot.
void debugOpenMenuDrawer(bool open);

// Optional song-select backdrop (the blurred desktop wallpaper). `texture` 0
// falls back to the built-in gradient. texW/texH are the texture's pixel size
// (used to cover the screen without distorting it) and `dim` (0..1) is a dark
// overlay drawn on top so the white text stays readable. The host owns the
// texture and calls this again whenever the setting changes.
void setSelectBackdrop(GLuint texture, int texW, int texH, float dim);

// "Aero glass" mode: draw no background fill at all (neither the wallpaper nor
// the built-in gradient), leaving the window transparent so the desktop shows
// through. The floating triangle field and every panel/list on top stay - this
// only removes the flat wash behind them. Needs the window's alpha channel and
// DWM's extended frame, which the host sets up (see main.cpp).
void setSelectTransparentBackground(bool enabled);

// Official per-difficulty levels, keyed by song id. unipjsk exports have their
// SUS header stripped (no #TITLE / #PLAYLEVEL, "#DIFFICULTY 0"), so the level
// has to come from the game's own data. Two formats are accepted:
//   { "75": [6, 13, 17, 23, 28], ... }   // easy, normal, hard, expert, master
//   [ { "musicId": 75, "musicDifficulty": "master", "playLevel": 28 }, ... ]
// The second is the game's own musicDifficulties.json verbatim, so it can be
// dropped in unchanged. May be called more than once; later entries win.
void loadMusicLevels(const std::string& path);

// Level of `difficulty` ("EASY".."MASTER") for a song id, 0 when unknown.
int musicLevel(int musicId, const std::string& difficulty);

// pjsk difficulty colour (EASY green / NORMAL blue / HARD orange / EXPERT red /
// MASTER purple / APPEND / ETERNAL), with `alpha`. Unknown difficulties fall
// back to a neutral slate. Shared by the song select badges and the result
// screen's difficulty capsule, which has to follow the actual difficulty.
ImU32 difficultyColor(const std::string& difficulty, int alpha = 255);

// Official song readings and titles from the game's musics.json: the reading
// drives "sort by name" and the aiueo grouping, the title fills in charts whose
// #TITLE is empty (every unipjsk export). Accepts the verbatim table (an array
// of objects with "id" / "pronunciation" / "title") or a compact map, either
// {"75": "よんぴき..."} (readings only) or
// {"75": {"kana": "...", "title": "..."}}. Songs without an entry fall back to
// whatever the chart itself carries. May be called more than once; later
// entries win.
void loadMusicMaster(const std::string& path);

// 原作的 musicCategories（master 表，导出成 music-categories.json，见
// .workbuddy/tools/gen_music_categories.py）。决定「絞り込み > MV」那一段：
//   {"75": ["mv", "mv_2d"], "400": ["mv_2d"], ...}
// `mv` = 3D MV、`mv_2d` = 2D MV、`original` = 原曲 MV；没有这三种的（master 里
// 只有 `image` 的）我们干脆不写进文件，等于「MVなし」。缺文件时整段 MV 过滤失效
// —— 原作在 musicCategories 表不可用时 `BuildCategories` 也返回空表。
void loadMusicCategories(const std::string& path);

// 一个 song id 的 MV 位掩码（bit0 3D / bit1 2D / bit2 原曲，0 = MVなし）。
unsigned musicMvMask(int musicId);

// 这首歌是不是「書き下ろし楽曲」（musics.json 的 isNewlyWrittenMusic）。
bool musicIsNewlyWritten(int musicId);

// Recursively collects *.sus under dir (bounded depth). Entries are sorted
// by title then file name.
std::vector<ChartEntry> scanChartFolder(const std::string& dir);

// "0075_master" -> "MASTER", "easy" -> "EASY"; empty when nothing matches.
std::string inferDifficulty(const std::string& name);

// Fills in the sidecar paths (bgm / jacket), difficulty and display name for
// a chart we already know about (e.g. one passed on the command line).
void resolveSidecars(ChartEntry& entry);

// 选曲列表的分组方式（settings.groupMode，见 ui 里的「分组」下拉）：
// 0 关闭 / 1 按难度段 / 2 按读音 / 3 按首字 / 4 按收藏。
// 存档读回来时要按这个上限 clamp —— 以前那里写死 3，加了「按收藏」之后会把 4 咬掉。
constexpr int kSelectGroupCount = 5;

// 团体页签的数量：数值与 CategoryTabType 一一对应（0..7）。
constexpr int kSelectUnitTabCount = 8;

enum SelectAction
{
    SelectNone = -1,
    SelectQuit = -2,
    SelectRescan = -3,
    SelectSettings = -4, // the musicsetting button was pressed (open settings)
    // The list is empty and the player pressed 下载谱面: the host launches
    // chartdl.exe (the standalone downloader) and re-scans when it exits.
    SelectDownload = -5,
};

// ---------------------------------------------------------------------------
// 多人游玩 on the song select (see platform/Party.hpp).
//
// The room has no screen of its own: the song list belongs to the host, so on
// every other window the list itself is read-only (nothing to scroll, nothing
// to pick) and the phone panel shows the song the host is parked on so a member
// picks a difficulty right there. Everything *around* the list stays live -
// 搜索 / 排序 / 分组 / 刷新 / 音乐商店 / 猜歌 / 设置 are per-window tools and
// were never the room's business. 确定 is what starts the round: every window
// presses it once and the live begins as soon as the last player has.
//
// Pass `party` (and `partyOut`) only when the shared room is live; leaving them
// out is plain single-window play.
// ---------------------------------------------------------------------------
struct SelectPartyInfo
{
    bool active = false; // the shared room is live
    bool host = false;   // this window is the host, so it keeps the list
    // The song the room is locked on, resolved against *this* window's own
    // chart list (-1 = this window does not have that chart, or nothing is
    // locked yet - `songLocked` tells the two apart).
    bool songLocked = false;
    int lockedEntry = -1;
    int myDifficulty = -1;  // canonical index (see game::difficultyIndex), -1 = none
    bool confirmed = false; // this window already pressed 确定 for this song
    std::string status;     // room line under the 确定 button
};

struct SelectPartyResult
{
    int difficulty = -1;   // the player picked this canonical difficulty index
    // 开始 was pressed on the final-confirm page. In 多人游玩 this marks the
    // seat ready and the round starts once every seat has done it; solo runs
    // never produce it (the page hands back the chart index instead).
    bool confirm = false;
    // The final-confirm page was left with 返回 / ESC: withdraw this seat from
    // the round the host is about to start.
    bool cancel = false;
};

// Draws the screen. `selected` is kept between frames; returns the index of
// the chart to start, SelectNone, or SelectQuit.
// `sortMode` / `groupMode` are in/out: the list's sort and grouping live in the
// settings, so they survive a restart (the caller persists them when they
// change). 筛选弹窗的三条条件（`clearFilter` / `mvUnchecked` / `musicFilter`，
// 数值就是原作的枚举，见 UserSettings）同样是 in/out：弹窗点「决定」时只改这三个
// 值，调用方照旧在变化后落盘。`vocalIndex` is out-only: the index (into
// availableVocals()) of the
// version currently picked for the selected song, so the caller can point the
// entry at that audio before starting. -1 = the song has no chooser at all.
// `uiScale` scales the whole 1080p canvas this screen is laid out on (1.0 =
// exactly fit the window, see UserSettings::uiScale). `confirmCenter` receives
// the on-screen centre of the 确定 button, which main.cpp uses as the origin of
// the white confirm flash (the button is drawn tilted, so this is the tilted
// position, not the layout one).
//
// In 多人游玩 a member's 确定 never comes back as a chart index: it arrives in
// `partyOut` instead (the room decides what starts, see SelectPartyInfo).
//
// `nativeMenuBar` (设置 > 系统) means the window carries a real Win32 menu bar,
// which owns 刷新谱面列表 / 音乐商店 / 设置 now - the header row drops those
// three buttons and keeps 猜歌, so the two never duplicate each other. The
// actions themselves are unchanged: main.cpp maps the menu ids onto the very
// same SelectRescan / SelectDownload / showDebug paths.
int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode, int& unitTab,
    int& clearFilter, int& mvUnchecked, int& musicFilter, int& vocalIndex,
    float uiScale = 1.0f, ImVec2* confirmCenter = nullptr, ImVec4* confirmPhoneScreen = nullptr,
    int deckPower = 250000, bool* autoLive = nullptr, int* mvMode = nullptr,
    const AccountData* account = nullptr,
    const SelectPartyInfo* party = nullptr, SelectPartyResult* partyOut = nullptr,
    int* favoriteToggle = nullptr, bool nativeMenuBar = false, int* deleteRequest = nullptr,
    // 6 live band magnitudes 0..1 for the selected card's equalizer (see
    // AudioEngine::previewSpectrum). Null = no measurement (headless / muted).
    const float* previewBands = nullptr,
    // The decide burst is owned by main (it also feeds the white flash). The
    // final-confirm page spawns it from its own start button, so it can play
    // on the final page instead of after switching back to the list.
    DecideBurst* decideBurst = nullptr);

// Debug (`--profile`): force the profile card open. It normally only appears
// when the level chip is clicked, which a --screenshot run cannot do.
void debugOpenProfileCard(bool open);
// Debug (`--select-filter`): open the 筛选 card for a --screenshot run.
void debugOpenFilterCard(bool open);

// 猜歌（header 那颗「猜歌」按钮 → 卡片）。打开是**异步**的：卡片立刻放上来显示
// 「正在准备题库…」，别名表在随后的帧里按 ~3ms/帧 建好（第一次打开要过 12.9k 个别名，
// 同步做会把点击那一帧卡住几百毫秒）。建完之后一直缓存。
void openGuessDialog();

// Debug (`--guess`): open the 猜歌 quiz (the header button's card) at boot, for
// the same reason. Also usable to close it again.
void debugOpenGuessDialog(bool open);

// True while the 猜歌 card is up. The main loop checks this before acting on
// ESC: with the card open, ESC belongs to the card (it closes it) and must not
// reach the "leave the live / quit the game" branch. That branch runs *before*
// drawSongSelect in a frame, so it reads the previous frame's state - which is
// exactly what makes it work: the frame ESC is pressed, the card is still open.
bool guessDialogOpen();
// 筛选卡片是否开着（ESC 链要问它，否则按 ESC 会直接退出选曲页）。
bool filterDialogOpen();
// 最终确认页是否开着（main 的 Enter / ESC 分支要区分列表页和确认页）。
bool finalConfirmOpen();
// 多人游玩: the shared charge has taken over (the chart is loading / the start
// instant is armed), so the final page is done. Closes it without an action.
void dismissFinalConfirm();
// 最终页按下开始钮时已经从按钮上 spawn 过 decide burst 了；main 拿到 action
// 时不要再 spawn 第二份，直接把白闪接上。取走即清零。
bool finalConfirmBurstQueued();
void clearFinalConfirmBurstQueued();

// Debug (`--singer-panel`): open the 切换歌手 panel (the phone panel's singer
// button) at boot, same reason. It only appears on a song that has a vocal
// version table, so pair it with --select-id / --select-vocal.
void debugOpenSingerPanel(bool open);
// Debug: open the final-confirm page for `entry` as if 确定 had been pressed
// (used by --party-auto so a headless round covers the real page).
void debugRequestFinalConfirm(int entryIndex, int difficulty, int vocalIndex, float timeSec);
// True while the 切换歌手 drawer is up. main's ESC chain checks it so ESC closes
// the drawer instead of leaving the song select / quitting the game.
bool singerPanelOpen();

} // namespace game
