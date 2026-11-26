// CppSekai - multi-instance co-op transport (see Party.hpp).
#include "Party.hpp"
#include "platform/Log.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace platform
{
namespace
{
// Layout of the shared pages. Every instance of this binary agrees on it, so
// there is no version negotiation beyond the mapping's name.
constexpr int kSongKeyBytes = 160;
constexpr int kTitleBytes = 192;
// A seat whose heartbeat stopped this long ago is treated as gone. Generous on
// purpose: a window that is busy decoding a chart does not tick either, and
// reaping a live player mid-load would be far worse than a late cleanup.
constexpr LONG kStaleMs = 6000;
// v2: added trackEndMs (the host's run length).
// v3: added the per-seat lead-in skip counter (skipReq).
// v4: added the per-seat result block (P/G/Gr/B/M, maxCombo, flags) and the
//     host's award decision (awardGen / mvpSlot / superStarSlot) - the multi
//     result screen needs everybody's final stats, not just the live score.
// v5: the result block also carried the seat's chosen card bundle ("卡面").
// v6: that card picker is gone; seats now publish the player's uploaded avatar
//     path instead. The result block no longer carries it (the seat does), and
//     the path is much longer than a bundle name, so the layout grew.
// The layout is part of the protocol, so an old build simply opens a different
// mapping instead of reading these fields at the wrong offsets.
constexpr wchar_t kMappingName[] = L"Local\\CppSekai.Party.v6";

// Seat flags for the result block (SharedSeat::resFlags).
enum SeatResultFlag
{
    SeatResCleared = 1 << 0,
    SeatResFullCombo = 1 << 1,
    SeatResAllPerfect = 1 << 2,
};

struct SharedSeat
{
    volatile LONG alive;      // 0 = free, 1 = owned (published last)
    volatile LONG heartbeat;  // GetTickCount64 low 32 bits, written every frame
    volatile LONG ready;
    volatile LONG difficulty;
    volatile LONG seat;
    volatile LONG score;
    volatile LONG combo;
    volatile LONG lifePermille;
    // Lead-in skip requests from this seat: bumped by the 跳过 button, polled by
    // the host (see PartyLink::requestSkip). Per seat so no two windows ever
    // write the same word.
    volatile LONG skipReq;
    // ---- final result of the run that just ended (v4) --------------------
    // Written once by the owner when its live ends, tagged with the room's
    // resultGen so a stale block from the previous round is never mistaken for
    // this one. `resReady` is what the host waits on before it decides awards.
    volatile LONG resReady;
    volatile LONG resGen;
    volatile LONG resPerfect;
    volatile LONG resGreat;
    volatile LONG resGood;
    volatile LONG resBad;
    volatile LONG resMiss;
    volatile LONG resMaxCombo;
    volatile LONG resLifePermille;
    volatile LONG resScore;
    volatile LONG resFlags;
    // The player's uploaded avatar, as an absolute path owned by this window.
    // Text, not an id: every window is on the same machine, so the path is
    // enough to load it (see PartyLink::setAvatar).
    char avatar[kPartyAvatarBytes];
    char name[kPartyNameBytes];
};

struct SharedBlock
{
    // Seqlock around the control fields (host-written): odd while a write is in
    // flight. Single LONGs outside it (hostSlot, the seats) are atomic on their
    // own and never need it.
    volatile LONG seq;
    volatile LONG phase;
    volatile LONG epoch;
    volatile LONG hostSlot;
    volatile LONG paused;
    volatile LONG musicId;
    volatile LONG hostDifficulty;
    volatile LONG leadInMs;
    volatile LONG startLo;
    volatile LONG startHi;
    // The host's live chart clock: milliseconds + the QPC it was sampled on.
    // Written every frame while the live runs, read (not written) by everyone
    // else, who then extrapolate it with their own QPC - the offset between the
    // two clocks is what they correct for.
    volatile LONG hostSongMs;
    volatile LONG hostClockLo;
    volatile LONG hostClockHi;
    // 0 until the host has published its first sample. Without it a member that
    // reads the block early pairs a zeroed clock with its own "now" - a whole
    // QPC worth of error (~hours), which snaps its chart clock into the future
    // and marks the entire chart missed.
    volatile LONG hostClockValid;
    // Length of the live the host is running, in milliseconds (0 = not
    // published yet). Written once per run, right after the host's chart loads.
    volatile LONG trackEndMs;
    // ---- multi result awards (v4, host only) ------------------------------
    // `resultGen` is bumped by the host every time a run starts charging; it is
    // the round id the per-seat result blocks are tagged with. `awardGen` is the
    // round the decision below belongs to, so a reader can tell "decided for the
    // round I am showing" from "left over from the previous one".
    //
    // MVP / SUPER STAR are the *host's* call because the official client does not
    // compute them either - the server hands every score row a `mvpFlg` /
    // `superStarFlg` (see MULTI-RESULT-MVP.md 4.1). We have no server, so the
    // host plays that part: it is the only window that sees everybody's stats.
    volatile LONG resultGen;
    volatile LONG awardGen;
    volatile LONG mvpSlot;
    volatile LONG superStarSlot;
    char songKey[kSongKeyBytes];
    char songTitle[kTitleBytes];
    SharedSeat seats[kPartyMaxSlots];
};

SharedBlock* gBlock = nullptr;

LONG nowMs()
{
    return static_cast<LONG>(GetTickCount64());
}

// Copies UTF-8 text into a fixed buffer without splitting a code point, so a
// truncated name never renders as a broken glyph.
void copyUtf8(char* dst, std::size_t size, const std::string& text)
{
    if (size == 0) {
        return;
    }
    std::size_t n = std::min(text.size(), size - 1);
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) {
        --n; // do not cut in the middle of a multi-byte sequence
    }
    std::memcpy(dst, text.data(), n);
    dst[n] = '\0';
}

std::string readUtf8(const char* src, std::size_t size)
{
    std::size_t n = 0;
    while (n < size && src[n] != '\0') {
        ++n;
    }
    return std::string(src, n);
}

std::uint64_t packPair(LONG lo, LONG hi)
{
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(lo))
        | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32);
}
} // namespace

bool traceEnabled()
{
    const char* env = std::getenv("CPSEKAI_MP_TRACE");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
}

std::uint64_t PartyLink::nowCounter()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

double PartyLink::counterFrequency()
{
    static const double freq = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart > 0 ? static_cast<double>(value.QuadPart) : 0.0;
    }();
    return freq > 0.0 ? freq : 1.0;
}

double PartyLink::counterToSeconds(std::uint64_t counter)
{
    return static_cast<double>(counter) / counterFrequency();
}

bool PartyLink::roomExists()
{
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMappingName);
    if (mapping == nullptr) {
        return false;
    }
    CloseHandle(mapping);
    return true;
}

bool PartyLink::init()
{
    if (mActive) {
        return true;
    }
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(SharedBlock)), kMappingName);
    if (mapping == nullptr) {
        platform::logf(platform::LogParty, "[party] CreateFileMappingW failed (%lu)\n", GetLastError());
        return false;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock));
    if (view == nullptr) {
        platform::logf(platform::LogParty, "[party] MapViewOfFile failed (%lu)\n", GetLastError());
        CloseHandle(mapping);
        return false;
    }
    mMapping = mapping;
    mView = view;
    gBlock = static_cast<SharedBlock*>(view);

    // Claim the lowest free seat. The owner of seat 0 that nobody else can take
    // is what makes the first window the host; if that window dies the role
    // moves on (see update()).
    //
    // The claim is a CAS on `alive`, and it is the *last* thing written for the
    // seat: everything else goes in first and `alive` is published with a
    // release fence. Publishing it first (the obvious `CAS(alive,1,0)` then
    // fill the fields) opens a window where another window's reaper sees
    // alive==1 next to the previous owner's stale heartbeat - which may already
    // be past kStaleMs - and reaps the seat before the joiner has written a
    // single field. That is the "连不上": the room loses members as fast as
    // they join, so the host never sees a second player and waits forever.
    const LONG tick = nowMs();
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        if (gBlock->seats[i].alive != 0) {
            continue;
        }
        SharedSeat& seat = gBlock->seats[i];
        seat.heartbeat = tick;
        seat.ready = 0;
        seat.difficulty = -1;
        // A window that just joined is *in* the round from this moment (it is
        // sitting in the room, on the song select), so the host waits for its
        // 确定 instead of starting without it. Only a window that never confirms
        // (or a seat the previous round left behind) falls back to the lobby.
        seat.seat = PartySeatChoosing;
        seat.score = 0;
        seat.combo = 0;
        seat.lifePermille = 1000;
        seat.skipReq = 0;
        seat.resReady = 0;
        seat.resGen = 0;
        seat.resPerfect = 0;
        seat.resGreat = 0;
        seat.resGood = 0;
        seat.resBad = 0;
        seat.resMiss = 0;
        seat.resMaxCombo = 0;
        seat.resLifePermille = 0;
        seat.resScore = 0;
        seat.resFlags = 0;
        seat.avatar[0] = '\0';
        seat.name[0] = '\0';
        std::atomic_thread_fence(std::memory_order_release);
        if (InterlockedCompareExchange(&seat.alive, 1, 0) != 0) {
            continue; // somebody else took this one while we were writing it
        }
        mSlot = i;
        break;
    }
    if (mSlot < 0) {
        platform::logf(platform::LogParty, "[party] no free seat (%d players already)\n", kPartyMaxSlots);
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        mView = nullptr;
        mMapping = nullptr;
        gBlock = nullptr;
        return false;
    }
    mActive = true;
    // First live seat becomes the host. Racing here is harmless: both writers
    // pick the same value (the lowest live seat).
    LONG host = gBlock->hostSlot;
    if (host < 0 || host >= kPartyMaxSlots || !gBlock->seats[host].alive) {
        gBlock->hostSlot = mSlot;
    }
    platform::logf(platform::LogParty, "[party] seat %d claimed (%d player(s), host seat %ld)\n", mSlot, playerCount(),
        static_cast<long>(gBlock->hostSlot));
    std::fflush(stdout);
    return true;
}

void PartyLink::shutdown()
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    if (isHost()) {
        // Let somebody else take over immediately instead of waiting out the
        // heartbeat timeout.
        gBlock->phase = PartyLobby;
        gBlock->startLo = 0;
        gBlock->startHi = 0;
        LONG next = -1;
        for (int i = 0; i < kPartyMaxSlots; ++i) {
            if (i != mSlot && gBlock->seats[i].alive) {
                next = i;
                break;
            }
        }
        gBlock->hostSlot = next;
    }
    InterlockedExchange(&gBlock->seats[mSlot].alive, 0);
    platform::logf(platform::LogParty, "[party] left seat %d\n", mSlot);
    std::fflush(stdout);
    gBlock = nullptr;
    mActive = false;
    if (mView != nullptr) {
        UnmapViewOfFile(mView);
        mView = nullptr;
    }
    if (mMapping != nullptr) {
        CloseHandle(static_cast<HANDLE>(mMapping));
        mMapping = nullptr;
    }
}

void PartyLink::update()
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    const LONG tick = nowMs();
    // A long chart load blocks this loop, and another window may have written
    // us off meanwhile: take the seat back instead of vanishing from the room.
    // Only if it is still marked free: between the reap and this line, a third
    // window can have gone through init() and legitimately claimed this seat,
    // and two processes both believing they own it is worse than being kicked
    // out (both would overwrite each other's heartbeat, name and difficulty
    // every frame - the member that "keeps dropping out" while still being
    // visible). If somebody is there, we lost the seat for real and rejoin.
    if (gBlock->seats[mSlot].alive == 0) {
        if (InterlockedCompareExchange(&gBlock->seats[mSlot].alive, 1, 0) != 0) {
            gBlock = nullptr;
            mActive = false;
            if (mView != nullptr) {
                UnmapViewOfFile(mView);
                mView = nullptr;
            }
            if (mMapping != nullptr) {
                CloseHandle(static_cast<HANDLE>(mMapping));
                mMapping = nullptr;
            }
            platform::logf(platform::LogParty, "[party] seat %d went to somebody else, rejoining\n", mSlot);
            std::fflush(stdout);
            if (init()) {
                platform::logf(platform::LogParty, "[party] rejoined as seat %d (%s)\n", mSlot,
                    isHost() ? "host" : "member");
            } else {
                platform::logf(platform::LogParty, "[party] rejoin failed, continuing solo\n");
            }
            std::fflush(stdout);
            return;
        }
        platform::logf(platform::LogParty, "[party] seat %d reclaimed after a stall\n", mSlot);
        std::fflush(stdout);
    }
    gBlock->seats[mSlot].heartbeat = tick;

    const LONG phase = gBlock->phase;
    if (phase != PartyCharging) {
        // During the charge every window is busy loading a chart and none of
        // them is ticking; reaping then would scatter the room.
        for (int i = 0; i < kPartyMaxSlots; ++i) {
            if (i == mSlot || gBlock->seats[i].alive == 0) {
                continue;
            }
            const LONG hb = gBlock->seats[i].heartbeat;
            if (hb == 0) {
                continue; // just born, not yet ticked
            }
            if (static_cast<LONG>(tick - hb) > kStaleMs) {
                InterlockedExchange(&gBlock->seats[i].alive, 0);
                platform::logf(platform::LogParty, "[party] seat %d timed out\n", i);
                std::fflush(stdout);
            }
        }
    }
    const LONG host = gBlock->hostSlot;
    if (host < 0 || host >= kPartyMaxSlots || !gBlock->seats[host].alive) {
        for (int i = 0; i < kPartyMaxSlots; ++i) {
            if (gBlock->seats[i].alive) {
                gBlock->hostSlot = i;
                if (i == mSlot) {
                    platform::logf(platform::LogParty, "[party] host role taken over by seat %d\n", i);
                    std::fflush(stdout);
                }
                break;
            }
        }
    }
}

bool PartyLink::isHost() const
{
    return mActive && gBlock != nullptr && gBlock->hostSlot == mSlot;
}

bool PartyLink::hostAlive() const
{
    if (!mActive || gBlock == nullptr) {
        return false;
    }
    const LONG host = gBlock->hostSlot;
    return host >= 0 && host < kPartyMaxSlots && gBlock->seats[host].alive != 0;
}

int PartyLink::playerCount() const
{
    if (!mActive || gBlock == nullptr) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        if (gBlock->seats[i].alive) {
            ++count;
        }
    }
    return count;
}

void PartyLink::publishHostClock(double songTimeSec)
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    const LONG ms = static_cast<LONG>(std::llround(songTimeSec * 1000.0));
    const std::uint64_t counter = nowCounter();
    gBlock->hostSongMs = ms;
    gBlock->hostClockLo = static_cast<LONG>(static_cast<std::uint32_t>(counter & 0xffffffffull));
    gBlock->hostClockHi = static_cast<LONG>(static_cast<std::uint32_t>(counter >> 32));
    gBlock->hostClockValid = 1;
}

bool PartyLink::readHostClock(double& songTimeSec, std::uint64_t& counter) const
{
    if (!mActive || gBlock == nullptr) {
        return false;
    }
    SharedBlock* block = gBlock;
    if (block->hostClockValid == 0) {
        return false; // no live has started here yet
    }
    for (int attempt = 0; attempt < 32; ++attempt) {
        const LONG ms = block->hostSongMs;
        const LONG lo = block->hostClockLo;
        const LONG hi = block->hostClockHi;
        if (block->hostSongMs != ms) {
            continue; // the host wrote while we were reading: take the next sample
        }
        if (lo == 0 && hi == 0) {
            return false; // torn read across the host's own publish
        }
        songTimeSec = static_cast<double>(ms) / 1000.0;
        counter = packPair(lo, hi);
        return true;
    }
    return false;
}

void PartyLink::setName(const std::string& name)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    copyUtf8(gBlock->seats[mSlot].name, kPartyNameBytes, name);
}

void PartyLink::setAvatar(const std::string& path)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    copyUtf8(gBlock->seats[mSlot].avatar, kPartyAvatarBytes, path);
}

void PartyLink::setSeat(int seat)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    gBlock->seats[mSlot].seat = seat;
}

void PartyLink::setReady(bool ready)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    gBlock->seats[mSlot].ready = ready ? 1 : 0;
    gBlock->seats[mSlot].seat = ready ? PartySeatReady : PartySeatChoosing;
}

void PartyLink::setDifficulty(int difficulty)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    gBlock->seats[mSlot].difficulty = difficulty;
}

void PartyLink::setPlayingScore(double score, int combo, float life)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    SharedSeat& seat = gBlock->seats[mSlot];
    seat.score = static_cast<LONG>(std::llround(score));
    seat.combo = combo;
    seat.lifePermille = static_cast<LONG>(std::lround(std::clamp(life, 0.0f, 1.0f) * 1000.0f));
}

void PartyLink::clearPlayingScore()
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    SharedSeat& seat = gBlock->seats[mSlot];
    seat.score = 0;
    seat.combo = 0;
    seat.lifePermille = 1000;
}

void PartyLink::requestSkip()
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    InterlockedIncrement(&gBlock->seats[mSlot].skipReq);
}

int PartyLink::skipTotal() const
{
    if (!mActive || gBlock == nullptr) {
        return 0;
    }
    int total = 0;
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        total += static_cast<int>(gBlock->seats[i].skipReq);
    }
    return total;
}

void PartyLink::lockSong(int musicId, const std::string& songKey, const std::string& title,
    int difficulty, int leadInMs)
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    InterlockedIncrement(&gBlock->seq);
    gBlock->musicId = musicId;
    gBlock->hostDifficulty = difficulty;
    gBlock->leadInMs = leadInMs;
    gBlock->paused = 0;
    gBlock->startLo = 0;
    gBlock->startHi = 0;
    gBlock->hostSongMs = 0;
    gBlock->hostClockValid = 0; // no clock for the new song until the host runs it
    gBlock->trackEndMs = 0;     // and no run length either
    copyUtf8(gBlock->songKey, kSongKeyBytes, songKey);
    copyUtf8(gBlock->songTitle, kTitleBytes, title);
    gBlock->epoch = gBlock->epoch + 1;
    gBlock->phase = PartySongLocked;
    InterlockedIncrement(&gBlock->seq);
    platform::logf(platform::LogParty, "[party] song locked: %s (%s) diff %d\n", songKey.c_str(), title.c_str(), difficulty);
    std::fflush(stdout);
}

void PartyLink::beginLoading()
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    InterlockedIncrement(&gBlock->seq);
    gBlock->paused = 0;
    // No instant yet: startLo/Hi stay 0, which is what tells every window (and
    // armStart below) that the charge is still loading. armStart() is what
    // fills them in - a reader that sees Charging + 0 knows to wait.
    gBlock->startLo = 0;
    gBlock->startHi = 0;
    gBlock->hostClockValid = 0;
    gBlock->epoch = gBlock->epoch + 1;
    // New round: the award decision from the previous one is void, and every
    // seat's result block is stale until it re-tags itself with this gen.
    gBlock->resultGen = gBlock->resultGen + 1;
    gBlock->awardGen = 0;
    gBlock->mvpSlot = -1;
    gBlock->superStarSlot = -1;
    gBlock->phase = PartyCharging;
    InterlockedIncrement(&gBlock->seq);
    platform::logf(platform::LogParty, "[party] loading: everybody ready, charts loading (epoch %ld)\n",
        static_cast<long>(gBlock->epoch));
    std::fflush(stdout);
}

void PartyLink::armStart(std::uint64_t startCounter)
{
    if (!mActive || gBlock == nullptr || !isHost() || startCounter == 0) {
        return;
    }
    if (gBlock->phase != PartyCharging) {
        return; // the room moved on (released, or locked another song) meanwhile
    }
    InterlockedIncrement(&gBlock->seq);
    gBlock->paused = 0;
    gBlock->startLo = static_cast<LONG>(static_cast<std::uint32_t>(startCounter & 0xffffffffull));
    gBlock->startHi = static_cast<LONG>(static_cast<std::uint32_t>(startCounter >> 32));
    gBlock->hostClockValid = 0;
    InterlockedIncrement(&gBlock->seq);
    platform::logf(platform::LogParty, "[party] start armed: counter %llu (epoch %ld)\n",
        static_cast<unsigned long long>(startCounter), static_cast<long>(gBlock->epoch));
    std::fflush(stdout);
}

void PartyLink::setPhase(int phase)
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    if (gBlock->phase != phase) {
        gBlock->seq += 1; // odd: a single-field write, the seqlock keeps readers out
        gBlock->phase = phase;
        gBlock->seq += 1;
    }
}

void PartyLink::setHostPaused(bool paused)
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    gBlock->paused = paused ? 1 : 0;
}

void PartyLink::publishTrackEnd(double seconds)
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    gBlock->trackEndMs = static_cast<LONG>(std::llround(std::max(0.0, seconds) * 1000.0));
    if (seconds > 1.0) {
        platform::logf(platform::LogParty, "[party] run length %.2fs published\n", seconds);
        std::fflush(stdout);
    }
}

double PartyLink::readTrackEnd() const
{
    if (!mActive || gBlock == nullptr) {
        return 0.0;
    }
    return static_cast<double>(gBlock->trackEndMs) / 1000.0;
}

void PartyLink::releaseSong()
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    InterlockedIncrement(&gBlock->seq);
    gBlock->phase = PartyLobby;
    gBlock->epoch = gBlock->epoch + 1;
    gBlock->paused = 0;
    gBlock->startLo = 0;
    gBlock->startHi = 0;
    gBlock->hostSongMs = 0;
    gBlock->hostClockValid = 0;
    gBlock->trackEndMs = 0;
    InterlockedIncrement(&gBlock->seq);
    platform::logf(platform::LogParty, "[party] back to the lobby\n");
    std::fflush(stdout);
}

PartyState PartyLink::read() const
{
    PartyState state;
    if (!mActive || gBlock == nullptr) {
        return state;
    }
    SharedBlock* block = gBlock;
    for (int attempt = 0; attempt < 128; ++attempt) {
        const LONG before = block->seq;
        if (before & 1) {
            continue; // a write is in flight
        }
        state.phase = block->phase;
        state.epoch = block->epoch;
        state.hostSlot = block->hostSlot;
        state.paused = block->paused != 0;
        state.musicId = block->musicId;
        state.hostDifficulty = block->hostDifficulty;
        state.leadInMs = block->leadInMs;
        const LONG lo = block->startLo;
        const LONG hi = block->startHi;
        state.songKey = readUtf8(block->songKey, kSongKeyBytes);
        state.songTitle = readUtf8(block->songTitle, kTitleBytes);
        _ReadWriteBarrier();
        if (block->seq == before) {
            state.startCounter = packPair(lo, hi);
            state.valid = true;
            break;
        }
    }
    return state;
}

std::vector<PartyPlayer> PartyLink::players() const
{
    std::vector<PartyPlayer> list;
    if (!mActive || gBlock == nullptr) {
        return list;
    }
    const LONG host = gBlock->hostSlot;
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        const SharedSeat& seat = gBlock->seats[i];
        if (seat.alive == 0) {
            continue;
        }
        PartyPlayer player;
        player.slot = i;
        player.host = (i == host);
        player.name = readUtf8(seat.name, kPartyNameBytes);
        if (player.name.empty()) {
            player.name = "玩家 " + std::to_string(i + 1);
        }
        player.avatar = readUtf8(seat.avatar, kPartyAvatarBytes);
        player.ready = seat.ready != 0;
        player.difficulty = seat.difficulty;
        player.seat = seat.seat;
        player.score = static_cast<double>(seat.score);
        player.combo = seat.combo;
        player.life = static_cast<float>(seat.lifePermille) / 1000.0f;
        list.push_back(player);
    }
    std::sort(list.begin(), list.end(),
        [](const PartyPlayer& a, const PartyPlayer& b) { return a.slot < b.slot; });
    return list;
}

bool PartyLink::allReady() const
{
    const std::vector<PartyPlayer> list = players();
    int inRoom = 0;
    for (const PartyPlayer& player : list) {
        if (player.seat == PartySeatLobby) {
            continue; // not in this round (never confirmed, or sat the last one out)
        }
        ++inRoom;
        if (!player.ready) {
            return false;
        }
    }
    // One player is enough: the host alone in the room starts the moment it
    // confirms, exactly like the single-window case, and a second window that
    // joins later catches the charge instead of blocking it.
    return inRoom >= 1;
}

bool PartyLink::allLoaded() const
{
    const std::vector<PartyPlayer> list = players();
    int inRoom = 0;
    for (const PartyPlayer& player : list) {
        if (player.seat == PartySeatLobby) {
            continue; // sitting this round out: not waited for
        }
        ++inRoom;
        // Playing counts too: the host may re-read the block in the same frame
        // the instant lands and a fast window has already crossed over.
        if (player.seat != PartySeatLoaded && player.seat != PartySeatPlaying) {
            return false;
        }
    }
    return inRoom >= 1;
}

// ---------------------------------------------------------------------------
// Multi result: per-seat final stats + the host's award decision.
//
// The official client never decides who is MVP - the server hands every score
// row its own `mvpFlg` / `superStarFlg` (dump.cs TypeDefIndex 3698, see
// MULTI-RESULT-MVP.md 4.1), and the cell just does SetActive on them. We have no
// server, so the host plays that part: it is the only window that can see
// everybody's stats, and it publishes the answer into the control block so all
// windows show the same winner even though each one draws its own screen.
//
// The rule is ours, not the official one (which is server-side and unknown):
//   MVP        - 存活者里分数最高；全都没存活时退化成"分数最高"。
//                并列按原作 CompareRows 的顺序比：Score -> Perfect ->
//                MaxCombo -> Life(MultiLiveUtility.CompareRows, RVA 0x4FDB6E8).
//   SUPER STAR - 存活 + All Perfect 的人里分数最高的那个，且不与 MVP 重复
//                （缎带只有一个坑，原作 mvp 和 superStar 两个节点同位置）。
// Anything either of these could not prove from the published stats stays
// undecided (-1) rather than guessed.
// ---------------------------------------------------------------------------

void PartyLink::publishResult(const PartyResult& result)
{
    if (!mActive || gBlock == nullptr) {
        return;
    }
    SharedSeat& seat = gBlock->seats[mSlot];
    const LONG gen = gBlock->resultGen;
    // Fill the payload first, flag it last: a reader that still sees the old
    // resReady keeps the old numbers, which is exactly the previous round.
    seat.resPerfect = result.perfect;
    seat.resGreat = result.great;
    seat.resGood = result.good;
    seat.resBad = result.bad;
    seat.resMiss = result.miss;
    seat.resMaxCombo = result.maxCombo;
    seat.resScore = static_cast<LONG>(std::llround(result.score));
    seat.resLifePermille = static_cast<LONG>(std::lround(std::clamp(result.life, 0.0f, 1.0f) * 1000.0f));
    LONG flags = 0;
    if (result.cleared) {
        flags |= SeatResCleared;
    }
    if (result.fullCombo) {
        flags |= SeatResFullCombo;
    }
    if (result.allPerfect) {
        flags |= SeatResAllPerfect;
    }
    seat.resFlags = flags;
    seat.resGen = gen;
    std::atomic_thread_fence(std::memory_order_release);
    seat.resReady = 1;
}

bool PartyLink::allResultsIn() const
{
    if (!mActive || gBlock == nullptr) {
        return false;
    }
    const LONG gen = gBlock->resultGen;
    int inRound = 0;
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        const SharedSeat& seat = gBlock->seats[i];
        if (seat.alive == 0) {
            continue;
        }
        const bool reported = seat.resReady != 0 && seat.resGen == gen;
        if (reported) {
            ++inRound;
            continue;
        }
        // A seat that is on the song select and has *not* reported never played
        // this round, so it is not waited for. (One that reported and then walked
        // back to the select screen is already counted above, which is what stops
        // a fast player's 继续 from making the host decide too early.)
        if (seat.seat == PartySeatLobby) {
            continue;
        }
        return false;
    }
    return inRound >= 1;
}

bool PartyLink::decideAwards()
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return false;
    }
    const LONG gen = gBlock->resultGen;
    if (gBlock->awardGen == gen && gBlock->mvpSlot >= 0) {
        return true; // already decided for this round
    }
    if (!allResultsIn()) {
        return false;
    }

    struct Row
    {
        int slot = -1;
        long long score = 0;
        int perfect = 0;
        int maxCombo = 0;
        int life = 0;
        bool cleared = false;
        bool allPerfect = false;
    };
    std::vector<Row> rows;
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        const SharedSeat& seat = gBlock->seats[i];
        if (seat.alive == 0 || seat.seat == PartySeatLobby) {
            continue;
        }
        if (seat.resReady == 0 || seat.resGen != gen) {
            continue;
        }
        Row row;
        row.slot = i;
        row.score = seat.resScore;
        row.perfect = seat.resPerfect;
        row.maxCombo = seat.resMaxCombo;
        row.life = seat.resLifePermille;
        row.cleared = (seat.resFlags & SeatResCleared) != 0;
        row.allPerfect = (seat.resFlags & SeatResAllPerfect) != 0;
        rows.push_back(row);
    }
    if (rows.empty()) {
        return false;
    }

    // Same lexicographic order the official ranking uses, so a tie lands on the
    // same player the game would have ranked first.
    const auto better = [](const Row& a, const Row& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.perfect != b.perfect) {
            return a.perfect > b.perfect;
        }
        if (a.maxCombo != b.maxCombo) {
            return a.maxCombo > b.maxCombo;
        }
        return a.life > b.life;
    };

    // MVP: cleared players first; if the whole room failed, fall back to the
    // whole room (somebody has to hold the ribbon, and the official screen does
    // show it on a failed run too).
    const Row* mvp = nullptr;
    for (const Row& row : rows) {
        if (!row.cleared) {
            continue;
        }
        if (mvp == nullptr || better(row, *mvp)) {
            mvp = &row;
        }
    }
    if (mvp == nullptr) {
        for (const Row& row : rows) {
            if (mvp == nullptr || better(row, *mvp)) {
                mvp = &row;
            }
        }
    }

    const Row* superStar = nullptr;
    for (const Row& row : rows) {
        if (row.slot == (mvp != nullptr ? mvp->slot : -1) || !row.cleared || !row.allPerfect) {
            continue;
        }
        if (superStar == nullptr || better(row, *superStar)) {
            superStar = &row;
        }
    }

    InterlockedIncrement(&gBlock->seq);
    gBlock->mvpSlot = mvp != nullptr ? mvp->slot : -1;
    gBlock->superStarSlot = superStar != nullptr ? superStar->slot : -1;
    gBlock->awardGen = gen;
    InterlockedIncrement(&gBlock->seq);
    platform::logf(platform::LogParty, "[party] awards: mvp=%d superStar=%d (%d rows)\n",
        static_cast<int>(gBlock->mvpSlot), static_cast<int>(gBlock->superStarSlot),
        static_cast<int>(rows.size()));
    std::fflush(stdout);
    return true;
}

int PartyLink::mvpSlot() const
{
    if (!mActive || gBlock == nullptr) {
        return -1;
    }
    if (gBlock->awardGen != gBlock->resultGen) {
        return -1; // stale decision from a previous round
    }
    return gBlock->mvpSlot;
}

int PartyLink::superStarSlot() const
{
    if (!mActive || gBlock == nullptr) {
        return -1;
    }
    if (gBlock->awardGen != gBlock->resultGen) {
        return -1;
    }
    return gBlock->superStarSlot;
}

std::vector<PartyResultRow> PartyLink::results() const
{
    std::vector<PartyResultRow> list;
    if (!mActive || gBlock == nullptr) {
        return list;
    }
    const LONG gen = gBlock->resultGen;
    const LONG host = gBlock->hostSlot;
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        const SharedSeat& seat = gBlock->seats[i];
        if (seat.alive == 0) {
            continue;
        }
        // Only "has reported for this round" matters - deliberately not the seat
        // state, so a player who already pressed 继续 does not vanish from
        // everybody else's members page mid-draw.
        if (seat.resReady == 0 || seat.resGen != gen) {
            continue;
        }
        PartyResultRow row;
        row.slot = i;
        row.host = (i == host);
        row.name = readUtf8(seat.name, kPartyNameBytes);
        if (row.name.empty()) {
            row.name = "玩家 " + std::to_string(i + 1);
        }
        row.perfect = seat.resPerfect;
        row.great = seat.resGreat;
        row.good = seat.resGood;
        row.bad = seat.resBad;
        row.miss = seat.resMiss;
        row.maxCombo = seat.resMaxCombo;
        row.score = static_cast<double>(seat.resScore);
        row.life = static_cast<float>(seat.resLifePermille) / 1000.0f;
        row.cleared = (seat.resFlags & SeatResCleared) != 0;
        row.fullCombo = (seat.resFlags & SeatResFullCombo) != 0;
        row.allPerfect = (seat.resFlags & SeatResAllPerfect) != 0;
        row.avatar = readUtf8(seat.avatar, kPartyAvatarBytes);
        list.push_back(row);
    }
    std::sort(list.begin(), list.end(), [](const PartyResultRow& a, const PartyResultRow& b) {
        return a.slot < b.slot;
    });
    return list;
}

} // namespace platform
