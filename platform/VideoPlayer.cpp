// CppSekai - the 2D MV decoder (child-process ffmpeg feeding a rawvideo pipe).
// See VideoPlayer.hpp for why it is a separate process instead of a linked
// library: licensing (LGPL stays outside this binary) and no MinGW .a has to be
// linked into a zig build.
#include "platform/VideoPlayer.hpp"

#include "path_utf8.hpp"
#include "platform/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace platform
{
namespace
{
// Fixed output size: every frame is exactly this many RGBA bytes, which is what
// makes reading the pipe a simple "read N bytes" loop. 640x360 is a touch above
// the source resolution (576x328) and keeps the pipe at ~22 MB/s at 24 fps.
constexpr int kOutWidth = 640;
constexpr int kOutHeight = 360;
constexpr int kDefaultFps = 24; // pjsk's 2D MV .usm is 24 fps (measured: 0036)

bool fileExists(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    std::FILE* probe = std::fopen(path.c_str(), "rb");
    if (probe == nullptr) {
        return false;
    }
    std::fclose(probe);
    return true;
}
} // namespace

VideoPlayer::~VideoPlayer()
{
    close();
}

std::string findFfmpeg(const std::string& hint)
{
    if (!hint.empty()) {
        return fileExists(hint) ? hint : std::string();
    }
#ifdef _WIN32
    // Next to the exe first (that is where a bundled trimmed build will live),
    // then the usual spots, then PATH.
    char module[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, module, MAX_PATH) > 0) {
        std::string dir(module);
        const std::size_t slash = dir.find_last_of("\\/");
        if (slash != std::string::npos) {
            const std::string candidate = dir.substr(0, slash + 1) + "ffmpeg.exe";
            if (fileExists(candidate)) {
                return candidate;
            }
        }
    }
    char found[MAX_PATH] = {0};
    if (SearchPathA(nullptr, "ffmpeg.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return std::string(found);
    }
#endif
    return std::string();
}

bool VideoPlayer::open(const std::string& ffmpegPath, const std::string& videoPath, int maxWidth,
    std::string& outError)
{
    close();
    outError.clear();

    if (!fileExists(videoPath)) {
        outError = "mv file not found: " + videoPath;
        return false;
    }
    const std::string binary = findFfmpeg(ffmpegPath);
    if (binary.empty()) {
        outError = "ffmpeg not found (put ffmpeg.exe next to the game, or set CPSEKAI_FFMPEG)";
        return false;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        outError = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
        OPEN_EXISTING, 0, nullptr);
    si.hStdOutput = writeEnd;
    si.hStdError = nul;
    si.hStdInput = nul;

    // -an: the 2D MV has no audio (the song track is played by the engine).
    // scale/pad force a fixed frame size so the reader never has to ask.
    int width = std::clamp(maxWidth > 0 ? maxWidth : kOutWidth, 160, 1920);
    if (width % 2 != 0) {
        ++width;
    }
    const int height = width * 9 / 16;
    std::string cmd = "\"" + binary + "\" -v error -nostdin -i \"" + videoPath + "\" -an"
        " -vf \"scale=" + std::to_string(width) + ":" + std::to_string(height)
        + ":force_original_aspect_ratio=decrease,"
          "pad=" + std::to_string(width) + ":" + std::to_string(height) + ":(ow-iw)/2:(oh-ih)/2\""
        " -pix_fmt rgba -f rawvideo -";
    std::wstring wcmd = path_utf8::widen(cmd);

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    if (!ok) {
        CloseHandle(readEnd);
        outError = "failed to start ffmpeg";
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    mProcess = new PROCESS_INFORMATION(pi);
    mReadPipe = readEnd;
    mBinary = binary;
    mWidth = width;
    mHeight = height;
    mFps = kDefaultFps;
    mFrameBytes = static_cast<std::size_t>(mWidth) * static_cast<std::size_t>(mHeight) * 4u;
    mBuffer.assign(mFrameBytes, 0);
    mFrame = -1;
    mEnded = false;

    logf(LogPlay, "[mv] ffmpeg=%s video=%s out=%dx%d frameBytes=%zu\n", binary.c_str(),
        videoPath.c_str(), mWidth, mHeight, mFrameBytes);
    std::fflush(stdout);
    return true;
#else
    (void)ffmpegPath;
    outError = "video playback is Windows-only for now";
    return false;
#endif
}

bool VideoPlayer::readOneFrame()
{
#ifdef _WIN32
    if (mReadPipe == nullptr || mEnded) {
        return false;
    }
    std::size_t got = 0;
    HANDLE pipe = static_cast<HANDLE>(mReadPipe);
    while (got < mFrameBytes) {
        DWORD chunk = 0;
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(mFrameBytes - got, 1u << 20));
        if (!ReadFile(pipe, mBuffer.data() + got, want, &chunk, nullptr) || chunk == 0) {
            // The child ended (or died). Keep whatever we have as the last frame.
            mEnded = true;
            return false;
        }
        got += chunk;
    }
    ++mFrame;
    return true;
#else
    return false;
#endif
}

bool VideoPlayer::pumpTo(int targetFrame)
{
    if (mReadPipe == nullptr) {
        return false;
    }
    if (targetFrame < 0) {
        targetFrame = 0;
    }
    // Forward only: the official player seeks with SetSeekPosition, and the
    // audio clock never runs backwards during a live (a retry restarts the
    // whole player, which the caller does by calling close()/open() again).
    while (mFrame < targetFrame && !mEnded) {
        if (!readOneFrame()) {
            break;
        }
    }
    return mFrame >= 0;
}

void VideoPlayer::close()
{
#ifdef _WIN32
    if (mReadPipe != nullptr) {
        CloseHandle(static_cast<HANDLE>(mReadPipe));
        mReadPipe = nullptr;
    }
    if (mProcess != nullptr) {
        auto* pi = static_cast<PROCESS_INFORMATION*>(mProcess);
        // Closing our end of the pipe already makes ffmpeg write into a closed
        // handle and exit, but do not rely on it - a hung child would keep a
        // decoder alive for the rest of the session.
        TerminateProcess(pi->hProcess, 0);
        CloseHandle(pi->hProcess);
        delete pi;
        mProcess = nullptr;
    }
#else
    mProcess = nullptr;
#endif
    mFrame = -1;
    mEnded = false;
    mBuffer.clear();
    mFrameBytes = 0;
}

} // namespace platform
