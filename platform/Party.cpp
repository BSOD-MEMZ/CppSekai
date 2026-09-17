// CppSekai - multi-instance co-op transport (see Party.hpp).
#include "Party.hpp"

#include <windows.h>

#include <algorithm>
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
constexpr wchar_t kMappingName[] = L"Local\\CppSekai.Party.v1";

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
        std::printf("[party] CreateFileMappingW failed (%lu)\n", GetLastError());
        return false;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock));
    if (view == nullptr) {
        std::printf("[party] MapViewOfFile failed (%lu)\n", GetLastError());
        CloseHandle(mapping);
        return false;
    }
    mMapping = mapping;
    mView = view;
    gBlock = static_cast<SharedBlock*>(view);

    // Claim the lowest free seat. The owner of seat 0 that nobody else can take
    // is what makes the first window the host; if that window dies the role
    // moves on (see update()).
    const LONG tick = nowMs();
    for (int i = 0; i < kPartyMaxSlots; ++i) {
        if (InterlockedCompareExchange(&gBlock->seats[i].alive, 1, 0) != 0) {
            continue;
        }
        SharedSeat& seat = gBlock->seats[i];
        seat.heartbeat = tick;
        seat.ready = 0;
        seat.difficulty = -1;
        seat.seat = PartySeatLobby;
        seat.score = 0;
        seat.combo = 0;
        seat.lifePermille = 1000;
        seat.name[0] = '\0';
        mSlot = i;
        break;
    }
    if (mSlot < 0) {
        std::printf("[party] no free seat (%d players already)\n", kPartyMaxSlots);
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
    std::printf("[party] seat %d claimed (%d player(s), host seat %ld)\n", mSlot, playerCount(),
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
    std::printf("[party] left seat %d\n", mSlot);
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
    if (gBlock->seats[mSlot].alive == 0) {
        InterlockedExchange(&gBlock->seats[mSlot].alive, 1);
        std::printf("[party] seat %d reclaimed after a stall\n", mSlot);
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
                std::printf("[party] seat %d timed out\n", i);
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
                    std::printf("[party] host role taken over by seat %d\n", i);
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
    copyUtf8(gBlock->songKey, kSongKeyBytes, songKey);
    copyUtf8(gBlock->songTitle, kTitleBytes, title);
    gBlock->epoch = gBlock->epoch + 1;
    gBlock->phase = PartySongLocked;
    InterlockedIncrement(&gBlock->seq);
    std::printf("[party] song locked: %s (%s) diff %d\n", songKey.c_str(), title.c_str(), difficulty);
    std::fflush(stdout);
}

void PartyLink::beginCharging(std::uint64_t startCounter)
{
    if (!mActive || gBlock == nullptr || !isHost()) {
        return;
    }
    InterlockedIncrement(&gBlock->seq);
    gBlock->paused = 0;
    gBlock->startLo = static_cast<LONG>(static_cast<std::uint32_t>(startCounter & 0xffffffffull));
    gBlock->startHi = static_cast<LONG>(static_cast<std::uint32_t>(startCounter >> 32));
    gBlock->hostClockValid = 0;
    gBlock->epoch = gBlock->epoch + 1;
    gBlock->phase = PartyCharging;
    InterlockedIncrement(&gBlock->seq);
    std::printf("[party] charging: start counter %llu (epoch %ld)\n",
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
    InterlockedIncrement(&gBlock->seq);
    std::printf("[party] back to the lobby\n");
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
            continue; // back on the song select: not part of this round
        }
        ++inRoom;
        if (!player.ready) {
            return false;
        }
    }
    return inRoom >= 2;
}

} // namespace platform
