// CppSekai - multi-instance co-op ("多人游玩").
//
// Several CppSekai windows on one machine play one song together: the first
// instance (the host) picks a song, every window picks its own difficulty, and
// all of them start on the same beat — with only the host playing the BGM.
//
// Transport is one named file mapping (Local\CppSekai.Party.v1) that every
// instance maps into its own address space, with one seat per player inside it.
// A state change is a plain store into those shared pages: no socket, no pipe,
// nothing to serialise and nothing to flush, so the other windows see it on
// their very next frame. On a single box that is as close to zero latency as a
// hand-off gets — the only delay left is the reader's own frame time.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform
{

constexpr int kPartyMaxSlots = 8;
constexpr int kPartyNameBytes = 40;

// Where the room as a whole is. Owned by the host.
enum PartyPhase
{
    PartyLobby = 0,      // free song select; nobody has locked a song
    PartySongLocked = 1, // the host picked a song; everyone picks a difficulty
    PartyCharging = 2,   // all ready: charts load, the start instant is public
    PartyRunning = 3,    // the live is on
};

// What one player is doing right now. Each instance writes only its own seat.
enum PartySeat
{
    PartySeatLobby = 0,
    PartySeatChoosing, // parked on the room screen, difficulty not picked
    PartySeatReady,
    PartySeatPlaying,
    PartySeatResult,
};

struct PartyPlayer
{
    int slot = -1;
    std::string name;
    bool ready = false;
    int difficulty = -1; // canonical index (0..4 EASY..MASTER), -1 = not picked
    int seat = PartySeatLobby;
    double score = 0.0;
    int combo = 0;
    float life = 1.0f;
    bool host = false;
};

// Consistent snapshot of the shared control block (seqlock read).
struct PartyState
{
    bool valid = false;
    int phase = PartyLobby;
    int epoch = 0;
    int hostSlot = -1;
    int musicId = 0;
    int hostDifficulty = -1;
    int leadInMs = 6000;
    bool paused = false;
    // QPC value the next session's clock starts on (chart time -leadIn). QPC is
    // a machine-wide counter, so every process can aim at the same instant.
    std::uint64_t startCounter = 0;
    std::string songKey; // chart file name the host locked
    std::string songTitle;
};

// Debug trace (CPSEKAI_MP_TRACE=1): one [sync] line per second per instance,
// carrying the shared QPC next to the local song clock so two logs can be
// diffed for drift.
bool traceEnabled();

class PartyLink
{
  public:
    // Maps the shared block and claims a seat. False = solo play (no mapping
    // available, or all 8 seats taken) — every caller then behaves exactly as
    // it did before multiplayer existed.
    bool init();
    void shutdown();
    bool active() const { return mActive; }

    // True when another window on this machine already has the room open.
    //
    // The 多人游玩 switch is stored per *profile*, the room is per machine, so
    // a second window that logged in as another user (multi-open) would
    // otherwise come up solo and the first window would never see two players -
    // which is exactly what "开了多人游玩却进不了房间" looks like. A window
    // that finds a room joins it whatever its own setting says.
    static bool roomExists();

    // Once per frame, before anything reads the room: keep this seat's
    // heartbeat fresh, drop seats whose owner died, and hand the host role over
    // when the old host is gone.
    void update();

    int slot() const { return mSlot; }
    bool isHost() const;
    bool hostAlive() const;
    int playerCount() const;

    // Host only: publish the current chart clock with the QPC it was sampled
    // on, so the other windows can follow the host's audio clock instead of
    // free-running on their own (see the follower in main.cpp). Only the host
    // has a decoded track, so only it can define song time.
    void publishHostClock(double songTimeSec);
    // Member side: latest (songTime, sampling instant) pair. False while the
    // host has not published anything yet.
    bool readHostClock(double& songTimeSec, std::uint64_t& counter) const;

    void setName(const std::string& name);
    void setSeat(int seat);
    void setReady(bool ready);
    void setDifficulty(int difficulty);
    // Live scoreboard of this seat (called every frame while playing).
    void setPlayingScore(double score, int combo, float life);
    void clearPlayingScore();

    // ---- host only -------------------------------------------------------
    // Publish the locked song and open the difficulty picker.
    void lockSong(int musicId, const std::string& songKey, const std::string& title, int difficulty,
        int leadInMs);
    // All ready: publish the absolute start instant (and the run's epoch).
    void beginCharging(std::uint64_t startCounter);
    void setPhase(int phase);
    void setHostPaused(bool paused);
    // Back to a free lobby (after a run, or when the host backs out).
    void releaseSong();

    // ---- shared reads ----------------------------------------------------
    PartyState read() const;
    std::vector<PartyPlayer> players() const;
    // True when every player parked on the room screen has picked a difficulty
    // (players who went back to the song select are skipped).
    bool allReady() const;

    static std::uint64_t nowCounter();
    static double counterFrequency();
    static double counterToSeconds(std::uint64_t counter);

  private:
    bool mActive = false;
    int mSlot = -1;
    void* mMapping = nullptr; // HANDLE
    void* mView = nullptr;    // SharedBlock*
};

} // namespace platform
