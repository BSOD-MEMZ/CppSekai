// CppSekai - Windows system media integration (see SystemMedia.hpp).
//
// Why this file hand-rolls the ABI: the build uses the bundled zig MinGW
// toolchain, which ships no Windows SDK and therefore no windows.media.h. The
// WinRT interfaces below are declared manually. Their vtable order and IIDs
// were read from the system metadata
// (C:\Windows\System32\WinMetadata\Windows.Media.winmd) - the declaration
// order of an interface's methods in metadata is its ABI vtable order.
#include "SystemMedia.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#endif

namespace platform
{
namespace
{
#ifdef _WIN32

    // ---- GUIDs (from Windows.Media.winmd / Windows.Foundation.winmd) ------
    // {99FA3FF4-1742-42A6-902E-087D41F965EC}
    const GUID IID_ISystemMediaTransportControls = {
        0x99FA3FF4, 0x1742, 0x42A6, {0x90, 0x2E, 0x08, 0x7D, 0x41, 0xF9, 0x65, 0xEC}};
    // {EA98D2F6-7F3C-4AF2-A586-72889808EFB1}
    // The versioned interface (winmd: Windows.Media.ISystemMediaTransportControls2).
    // UpdateTimelineProperties and the AutoRepeatMode / ShuffleEnabled /
    // PlaybackRate accessors live HERE, not on the base interface above.
    const GUID IID_ISystemMediaTransportControls2 = {
        0xEA98D2F6, 0x7F3C, 0x4AF2, {0xA5, 0x86, 0x72, 0x88, 0x98, 0x08, 0xEF, 0xB1}};
    // {8ABBC53E-FA55-4ECF-AD8E-C984E5DD1550}
    const GUID IID_IDisplayUpdater = {
        0x8ABBC53E, 0xFA55, 0x4ECF, {0xAD, 0x8E, 0xC9, 0x84, 0xE5, 0xDD, 0x15, 0x50}};
    // {6BBF0C59-D0A0-4D26-92A0-F978E1D18E7B}
    const GUID IID_IMusicDisplayProperties = {
        0x6BBF0C59, 0xD0A0, 0x4D26, {0x92, 0xA0, 0xF9, 0x78, 0xE1, 0xD1, 0x8E, 0x7B}};
    // {5125316A-C3A2-475B-8507-93534DC88F15}
    const GUID IID_ITimelineProperties = {
        0x5125316A, 0xC3A2, 0x475B, {0x85, 0x07, 0x93, 0x53, 0x4D, 0xC8, 0x8F, 0x15}};
    // {DDB0472D-C911-4A1F-86D9-DC3D71A95F5A} - also in MinGW's
    // systemmediatransportcontrolsinterop.h.
    const GUID IID_ISystemMediaTransportControlsInterop = {
        0xDDB0472D, 0xC911, 0x4A1F, {0x86, 0xD9, 0xDC, 0x3D, 0x71, 0xA9, 0x5F, 0x5A}};

    // {56FDF344-FD6D-11D0-958A-006097C9A090} / {EA1AFB91-9E28-4B86-90E9-9E9F8A5EEFAF}
    const GUID CLSID_TaskbarList_ = {
        0x56FDF344, 0xFD6D, 0x11D0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
    const GUID IID_ITaskbarList3_ = {
        0xEA1AFB91, 0x9E28, 0x4B86, {0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF}};

    // MediaPlaybackStatus (Windows.Media) - values from the official enum docs
    // (https://learn.microsoft.com/uwp/api/windows.media.mediaplaybackstatus):
    // Closed = 0, Changing = 1, Stopped = 2, Playing = 3, Paused = 4.
    //
    // This was off by one (3/4/5) for a long time, and that is exactly why the
    // system media flyout never showed a sane session: pushing "Playing" sent
    // 4, which the shell reads as PAUSED, so every session claimed to be paused
    // while the timeline kept advancing (and "Paused" was 5, out of range).
    enum : int { StatusStopped = 2, StatusPlaying = 3, StatusPaused = 4 };
    // MediaPlaybackType (Windows.Media): Unknown = 0, Music = 1, Video = 2, Image = 3.
    enum : int { PlaybackTypeMusic = 1 };

    constexpr unsigned long long kTicksPerSec = 10000000ULL; // 100ns units

    // ---- WinRT ABI vtables ------------------------------------------------
    // Every entry after the six IInspectable slots follows the metadata order.
    struct ISMTCVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* get_PlaybackStatus)(void*, int*);
        HRESULT (STDMETHODCALLTYPE* put_PlaybackStatus)(void*, int);
        HRESULT (STDMETHODCALLTYPE* get_DisplayUpdater)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* get_SoundLevel)(void*, int*);
        HRESULT (STDMETHODCALLTYPE* get_IsEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsPlayEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsPlayEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsStopEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsStopEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsPauseEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsPauseEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsRecordEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsRecordEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsFastForwardEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsFastForwardEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsRewindEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsRewindEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsPreviousEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsPreviousEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsNextEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsNextEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsChannelUpEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsChannelUpEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_IsChannelDownEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_IsChannelDownEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* add_ButtonPressed)(void*, void*, long long*);
        HRESULT (STDMETHODCALLTYPE* remove_ButtonPressed)(void*, long long);
        HRESULT (STDMETHODCALLTYPE* add_PropertyChanged)(void*, void*, long long*);
        HRESULT (STDMETHODCALLTYPE* remove_PropertyChanged)(void*, long long);
        // The base interface ENDS here - 30 methods after the six IInspectable
        // slots. AutoRepeatMode / ShuffleEnabled / PlaybackRate /
        // UpdateTimelineProperties are NOT part of it; they live on
        // ISystemMediaTransportControls2 (below). Appending them here makes the
        // call read past the end of this vtable and jump to whatever garbage
        // sits there - that segfaulted the moment a song started.
    };

    // ISystemMediaTransportControls2 - obtained with QueryInterface. Only the
    // last slot (index 6) is used; the leading members must match metadata
    // order so the offset is right.
    struct ISMTC2Vtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* get_AutoRepeatMode)(void*, int*);
        HRESULT (STDMETHODCALLTYPE* put_AutoRepeatMode)(void*, int);
        HRESULT (STDMETHODCALLTYPE* get_ShuffleEnabled)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* put_ShuffleEnabled)(void*, unsigned char);
        HRESULT (STDMETHODCALLTYPE* get_PlaybackRate)(void*, double*);
        HRESULT (STDMETHODCALLTYPE* put_PlaybackRate)(void*, double);
        HRESULT (STDMETHODCALLTYPE* UpdateTimelineProperties)(void*, void*);
    };

    struct IDisplayUpdaterVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* get_Type)(void*, int*);
        HRESULT (STDMETHODCALLTYPE* put_Type)(void*, int);
        HRESULT (STDMETHODCALLTYPE* get_AppMediaId)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* put_AppMediaId)(void*, void*);
        HRESULT (STDMETHODCALLTYPE* get_Thumbnail)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* put_Thumbnail)(void*, void*);
        HRESULT (STDMETHODCALLTYPE* get_MusicProperties)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* get_VideoProperties)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* get_ImageProperties)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* CopyFromFileAsync)(void*, int, void*, void**);
        HRESULT (STDMETHODCALLTYPE* ClearAll)(void*);
        HRESULT (STDMETHODCALLTYPE* Update)(void*);
    };

    struct IMusicPropertiesVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* get_Title)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* put_Title)(void*, void*);
        HRESULT (STDMETHODCALLTYPE* get_AlbumArtist)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* put_AlbumArtist)(void*, void*);
        HRESULT (STDMETHODCALLTYPE* get_Artist)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* put_Artist)(void*, void*);
    };

    struct ITimelineVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6. Windows.Foundation.TimeSpan is an 8-byte struct {INT64 Duration},
        // which the x64 ABI passes BY VALUE - it is NOT an IPropertyValue
        // pointer (passing a boxed pointer here made every timeline value a
        // garbage tick count and SMTC never showed a sensible progress bar).
        HRESULT (STDMETHODCALLTYPE* get_StartTime)(void*, long long*);
        HRESULT (STDMETHODCALLTYPE* put_StartTime)(void*, long long);
        HRESULT (STDMETHODCALLTYPE* get_EndTime)(void*, long long*);
        HRESULT (STDMETHODCALLTYPE* put_EndTime)(void*, long long);
        HRESULT (STDMETHODCALLTYPE* get_MinSeekTime)(void*, long long*);
        HRESULT (STDMETHODCALLTYPE* put_MinSeekTime)(void*, long long);
        HRESULT (STDMETHODCALLTYPE* get_MaxSeekTime)(void*, long long*);
        HRESULT (STDMETHODCALLTYPE* put_MaxSeekTime)(void*, long long);
        HRESULT (STDMETHODCALLTYPE* get_Position)(void*, long long*);
        HRESULT (STDMETHODCALLTYPE* put_Position)(void*, long long);
    };

    struct IInteropVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        HRESULT (STDMETHODCALLTYPE* GetForWindow)(void*, HWND, const GUID*, void**);
    };

    struct ITaskbarList3Vtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* HrInit)(void*);
        HRESULT (STDMETHODCALLTYPE* AddTab)(void*, HWND);
        HRESULT (STDMETHODCALLTYPE* DeleteTab)(void*, HWND);
        HRESULT (STDMETHODCALLTYPE* ActivateTab)(void*, HWND);
        HRESULT (STDMETHODCALLTYPE* SetActiveAlt)(void*, HWND);
        HRESULT (STDMETHODCALLTYPE* MarkFullscreenWindow)(void*, HWND, int);
        HRESULT (STDMETHODCALLTYPE* SetProgressValue)(void*, HWND, unsigned long long, unsigned long long);
        HRESULT (STDMETHODCALLTYPE* SetProgressState)(void*, HWND, int);
    };

    // obj points at the COM object; obj[0] is the vtable pointer.
    template <typename Vtbl>
    Vtbl* vt(void* obj)
    {
        return *reinterpret_cast<Vtbl**>(obj);
    }

    void safeRelease(void* obj)
    {
        if (obj == nullptr) {
            return;
        }
        vt<ISMTCVtbl>(obj)->Release(obj);
    }

    // ---- Dynamic WinRT entry points (no import library available) ---------
    using RoGetActivationFactoryFn = HRESULT(WINAPI*)(void* classId, const GUID* iid, void** factory);
    using RoActivateInstanceFn = HRESULT(WINAPI*)(void* classId, void** instance);
    using WindowsCreateStringFn = HRESULT(WINAPI*)(const wchar_t* src, unsigned length, void** str);
    using WindowsDeleteStringFn = HRESULT(WINAPI*)(void* str);
    using WindowsGetStringRawBufferFn = const wchar_t*(WINAPI*)(void* str, unsigned* length);

    HMODULE gCombase = nullptr;
    RoGetActivationFactoryFn gRoGetActivationFactory = nullptr;
    RoActivateInstanceFn gRoActivateInstance = nullptr;
    WindowsCreateStringFn gCreateString = nullptr;
    WindowsDeleteStringFn gDeleteString = nullptr;
    WindowsGetStringRawBufferFn gGetStringRawBuffer = nullptr;
    bool gWinrtLoaded = false;

    void loadWinrt()
    {
        if (gWinrtLoaded) {
            return;
        }
        gWinrtLoaded = true;
        gCombase = LoadLibraryW(L"combase.dll");
        if (gCombase == nullptr) {
            return;
        }
        gRoGetActivationFactory =
            reinterpret_cast<RoGetActivationFactoryFn>(GetProcAddress(gCombase, "RoGetActivationFactory"));
        gRoActivateInstance = reinterpret_cast<RoActivateInstanceFn>(GetProcAddress(gCombase, "RoActivateInstance"));
        gCreateString = reinterpret_cast<WindowsCreateStringFn>(GetProcAddress(gCombase, "WindowsCreateString"));
        gDeleteString = reinterpret_cast<WindowsDeleteStringFn>(GetProcAddress(gCombase, "WindowsDeleteString"));
        gGetStringRawBuffer =
            reinterpret_cast<WindowsGetStringRawBufferFn>(GetProcAddress(gCombase, "WindowsGetStringRawBuffer"));
    }

    // HSTRING wrapper. WinRT strings are ref-counted handles, not raw pointers.
    struct HString
    {
        void* handle = nullptr;

        explicit HString(const std::string& utf8)
        {
            if (gCreateString == nullptr || utf8.empty()) {
                return;
            }
            const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wlen <= 0) {
                return;
            }
            std::vector<wchar_t> wide(static_cast<size_t>(wlen));
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), wlen);
            if (FAILED(gCreateString(wide.data(), static_cast<unsigned>(wlen), &handle))) {
                handle = nullptr;
            }
        }

        explicit HString(const wchar_t* wide)
        {
            if (gCreateString == nullptr || wide == nullptr) {
                return;
            }
            const int wlen = static_cast<int>(wcslen(wide));
            if (FAILED(gCreateString(wide, static_cast<unsigned>(wlen), &handle))) {
                handle = nullptr;
            }
        }

        ~HString()
        {
            if (handle != nullptr && gDeleteString != nullptr) {
                gDeleteString(handle);
            }
        }

        HString(const HString&) = delete;
        HString& operator=(const HString&) = delete;
        bool valid() const { return handle != nullptr; }
    };

#else  // !_WIN32
    void loadWinrt() {}
#endif // _WIN32
} // namespace

bool SystemMedia::init(SDL_Window* window)
{
#ifdef _WIN32
    loadWinrt();

    if (window != nullptr) {
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE) {
            std::printf("[media] SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        } else {
            mWindow = info.info.win.window;
        }
    }

    // COM: SDL may already have initialized it; RPC_E_CHANGED_MODE is fine.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    mComInitialized = SUCCEEDED(com);

    // ---- Taskbar progress -------------------------------------------------
    void* taskbar = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList_, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3_,
            &taskbar))) {
        vt<ITaskbarList3Vtbl>(taskbar)->HrInit(taskbar);
        mTaskbar = taskbar;
        std::printf("[media] taskbar progress ready\n");
    }

    // ---- SMTC -------------------------------------------------------------
    if (gRoGetActivationFactory == nullptr || mWindow == nullptr) {
        std::printf("[media] SMTC unavailable (no WinRT or no window)\n");
        return true;
    }

    void* factory = nullptr;
    HString className(L"Windows.Media.SystemMediaTransportControls");
    if (FAILED(gRoGetActivationFactory(className.handle, &IID_ISystemMediaTransportControlsInterop, &factory))
        || factory == nullptr) {
        std::printf("[media] SMTC: activation factory unavailable\n");
        return true;
    }

    void* controls = nullptr;
    const HRESULT hr = vt<IInteropVtbl>(factory)->GetForWindow(factory, static_cast<HWND>(mWindow),
        &IID_ISystemMediaTransportControls, &controls);
    safeRelease(factory);
    if (FAILED(hr) || controls == nullptr) {
        std::printf("[media] SMTC: GetForWindow failed (0x%08lX)\n", static_cast<unsigned long>(hr));
        return true;
    }
    mSmtc = controls;

    auto* v = vt<ISMTCVtbl>(mSmtc);
    v->put_IsEnabled(mSmtc, 1);
    v->put_IsPlayEnabled(mSmtc, 0);
    v->put_IsPauseEnabled(mSmtc, 1);
    v->put_IsStopEnabled(mSmtc, 0);
    v->put_IsNextEnabled(mSmtc, 0);
    v->put_IsPreviousEnabled(mSmtc, 0);
    v->put_IsFastForwardEnabled(mSmtc, 0);
    v->put_IsRewindEnabled(mSmtc, 0);

    // Read-back sanity check: if the round trip works, the control object is
    // live and the system media flyout will pick the session up.
    unsigned char enabled = 0;
    v->get_IsEnabled(mSmtc, &enabled);
    std::printf("[media] SMTC ready (IsEnabled readback = %d)\n", static_cast<int>(enabled));
#else
    (void)window;
#endif
    return true;
}

void SystemMedia::shutdown()
{
#ifdef _WIN32
    if (mSmtc != nullptr) {
        safeRelease(mSmtc);
        mSmtc = nullptr;
    }
    if (mTaskbar != nullptr) {
        vt<ITaskbarList3Vtbl>(mTaskbar)->Release(mTaskbar);
        mTaskbar = nullptr;
    }
    if (mComInitialized) {
        CoUninitialize();
        mComInitialized = false;
    }
    mWindow = nullptr;
#endif
}

void SystemMedia::setTrack(const std::string& title, const std::string& artist, double durationSec)
{
#ifdef _WIN32
    if (mSmtc == nullptr) {
        return;
    }
    mTrackTitle = title;
    mTrackArtist = artist;
    mLastDurationSec = durationSec;

    // One-shot diagnostics. Metadata was pushed silently before, so a rejected
    // property (or an HSTRING that failed to build) looked exactly like the
    // shell ignoring us - which is how the off-by-one playback status above
    // stayed hidden.
    static bool s_metaDiagLogged = false;
    const auto diag = [&](const char* what, HRESULT hr) {
        if (!s_metaDiagLogged) {
            std::printf("[media] %s hr=0x%08lX (title='%s' artist='%s')\n", what,
                static_cast<unsigned long>(hr), title.c_str(), artist.c_str());
            std::fflush(stdout);
        }
    };

    void* updater = nullptr;
    const HRESULT updaterHr = vt<ISMTCVtbl>(mSmtc)->get_DisplayUpdater(mSmtc, &updater);
    diag("get_DisplayUpdater", updaterHr);
    if (FAILED(updaterHr) || updater == nullptr) {
        return;
    }
    diag("put_Type", vt<IDisplayUpdaterVtbl>(updater)->put_Type(updater, PlaybackTypeMusic));

    void* props = nullptr;
    const HRESULT propsHr = vt<IDisplayUpdaterVtbl>(updater)->get_MusicProperties(updater, &props);
    diag("get_MusicProperties", propsHr);
    if (SUCCEEDED(propsHr) && props != nullptr) {
        HString titleStr(title.empty() ? std::string("CppSekai") : title);
        HString artistStr(artist.empty() ? std::string("CppSekai") : artist);
        if (titleStr.valid()) {
            diag("put_Title", vt<IMusicPropertiesVtbl>(props)->put_Title(props, titleStr.handle));
        } else {
            diag("put_Title SKIPPED (HSTRING build failed)", E_FAIL);
        }
        if (artistStr.valid()) {
            diag("put_Artist", vt<IMusicPropertiesVtbl>(props)->put_Artist(props, artistStr.handle));
        }
        // Read one property back: a write that silently no-ops (wrong vtable
        // slot, wrong interface) is invisible otherwise.
        void* back = nullptr;
        if (SUCCEEDED(vt<IMusicPropertiesVtbl>(props)->get_Title(props, &back)) && back != nullptr) {
            if (gGetStringRawBuffer != nullptr && !s_metaDiagLogged) {
                unsigned len = 0;
                const wchar_t* text = gGetStringRawBuffer(back, &len);
                // Print the length + first code point instead of the text: the
                // MinGW printf here mangles a %ls of a UTF-16 buffer, and what
                // matters for the round trip is whether the property took the
                // value at all.
                std::printf("[media] Title readback: %u chars, first=U+%04X\n", len,
                    (text != nullptr && len > 0) ? static_cast<unsigned>(text[0]) : 0u);
                std::fflush(stdout);
            }
            if (gDeleteString != nullptr) {
                gDeleteString(back);
            }
        }
        safeRelease(props);
    }
    diag("Update", vt<IDisplayUpdaterVtbl>(updater)->Update(updater));
    s_metaDiagLogged = true;
    safeRelease(updater);

    mLastPositionSec = -1.0e9; // force a timeline push on the next update
    mLastStatus = -1;
#else
    (void)title;
    (void)artist;
    (void)durationSec;
#endif
}

void SystemMedia::updatePlayback(bool playing, bool paused, double positionSec, double durationSec)
{
#ifdef _WIN32
    if (durationSec > 0.0) {
        mLastDurationSec = durationSec;
    }
    const int status = paused ? StatusPaused : (playing ? StatusPlaying : StatusStopped);
    if (mSmtc == nullptr) {
        return;
    }
    if (status == mLastStatus && std::fabs(positionSec - mLastPositionSec) < 0.4) {
        return;
    }
    mLastStatus = status;
    mLastPositionSec = positionSec;

    // One-shot diagnostics: if the shell rejects our SMTC pushes this is the
    // only place it shows (the overlay just silently keeps stale data).
    static bool s_diagLogged = false;
    const auto diag = [&](const char* what, HRESULT hr) {
        if (!s_diagLogged) {
            std::printf("[media] %s hr=0x%08lX (pos=%.2fs end=%.2fs status=%d)\n", what,
                static_cast<unsigned long>(hr), positionSec, mLastDurationSec, status);
            std::fflush(stdout);
        }
    };

    const HRESULT statusHr = vt<ISMTCVtbl>(mSmtc)->put_PlaybackStatus(mSmtc, status);
    diag("put_PlaybackStatus", statusHr);

    // Read the status back in the same diagnostic: the shell's own idea of
    // "playing" is what the media flyout shows, so a mismatch here means the
    // MediaPlaybackStatus constant is wrong (that is how the old off-by-one
    // values slipped through - the write itself always returned S_OK).
    if (!s_diagLogged) {
        int readback = -1;
        if (SUCCEEDED(vt<ISMTCVtbl>(mSmtc)->get_PlaybackStatus(mSmtc, &readback))) {
            std::printf("[media] PlaybackStatus readback = %d (wrote %d)\n", readback, status);
            std::fflush(stdout);
        }
    }

    if (gRoActivateInstance == nullptr || gRoGetActivationFactory == nullptr) {
        return;
    }

    // TimelineProperties is a fresh object each time; SMTC copies the values.
    void* timeline = nullptr;
    HString timelineClass(L"Windows.Media.SystemMediaTransportControlsTimelineProperties");
    if (FAILED(gRoActivateInstance(timelineClass.handle, &timeline)) || timeline == nullptr) {
        return;
    }
    void* typed = nullptr;
    vt<ITimelineVtbl>(timeline)->QueryInterface(timeline, &IID_ITimelineProperties, &typed);
    if (typed == nullptr) {
        safeRelease(timeline);
        return;
    }

    auto* tv = vt<ITimelineVtbl>(typed);
    if (mLastDurationSec > 0.0) {
        // TimeSpan values go straight in: 8-byte struct by value (see the
        // vtable declaration), ticks = 100ns units.
        tv->put_StartTime(typed, 0);
        tv->put_EndTime(typed,
            static_cast<long long>(mLastDurationSec * static_cast<double>(kTicksPerSec)));
        tv->put_Position(typed,
            static_cast<long long>(std::max(0.0, positionSec) * static_cast<double>(kTicksPerSec)));
    }
    // UpdateTimelineProperties lives on ISystemMediaTransportControls2, NOT on
    // the base interface - query for it (and skip the position update when the
    // object does not expose it, e.g. a very old Windows build).
    void* smtc2 = nullptr;
    if (SUCCEEDED(vt<ISMTCVtbl>(mSmtc)->QueryInterface(mSmtc, &IID_ISystemMediaTransportControls2, &smtc2))
        && smtc2 != nullptr) {
        const HRESULT timelineHr = vt<ISMTC2Vtbl>(smtc2)->UpdateTimelineProperties(smtc2, typed);
        diag("UpdateTimelineProperties", timelineHr);
        s_diagLogged = true; // the first put + the first timeline push are enough
        safeRelease(smtc2);
    }
    safeRelease(typed);
    safeRelease(timeline);
#else
    (void)playing;
    (void)paused;
    (void)positionSec;
    (void)durationSec;
#endif
}

void SystemMedia::setTaskbarProgress(double ratio01, bool paused, bool indeterminate)
{
#ifdef _WIN32
    if (mTaskbar == nullptr || mWindow == nullptr) {
        return;
    }
    auto* taskbar = vt<ITaskbarList3Vtbl>(mTaskbar);
    const HWND hwnd = static_cast<HWND>(mWindow);

    int state = 2; // TBPF_NORMAL
    if (ratio01 < 0.0) {
        state = 0; // TBPF_NOPROGRESS
    } else if (indeterminate) {
        state = 1; // TBPF_INDETERMINATE
    } else if (paused) {
        state = 8; // TBPF_PAUSED
    }

    // Only touch COM when something visible changed (1% steps).
    const double quantized = ratio01 < 0.0 ? -1.0 : std::floor(ratio01 * 100.0) / 100.0;
    if (state == mLastTaskbarState && quantized == mLastTaskbarRatio) {
        return;
    }
    mLastTaskbarState = state;
    mLastTaskbarRatio = quantized;

    taskbar->SetProgressState(mTaskbar, hwnd, state);
    if (state == 2 || state == 8) {
        taskbar->SetProgressValue(mTaskbar, hwnd,
            static_cast<unsigned long long>(std::clamp(ratio01, 0.0, 1.0) * 10000.0), 10000ULL);
    }
#else
    (void)ratio01;
    (void)paused;
    (void)indeterminate;
#endif
}

} // namespace platform
