// CppSekai - Windows system media integration (see SystemMedia.hpp).
//
// Why this file hand-rolls the ABI: the build uses the bundled zig MinGW
// toolchain, which ships no Windows SDK and therefore no windows.media.h. The
// WinRT interfaces below are declared manually. Their vtable order and IIDs
// were read from the system metadata
// (C:\Windows\System32\WinMetadata\Windows.Media.winmd) - the declaration
// order of an interface's methods in metadata is its ABI vtable order.
#include "SystemMedia.hpp"

#include "path_utf8.hpp"

#include <filesystem>
#include <system_error>

#include <SDL.h>
#include <SDL_syswm.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
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
    // {6BBF0C59-D0A0-4D26-92A0-F978E1D18E7B}
    // ^ IID_IDisplayUpdater / IID_IMusicDisplayProperties 两张表已经不再需要：
    //   -Wunused-const-variable 查出来它们零引用（2026-09-21）。GUID 留在这两行
    //   注释里，将来要用直接照抄。
    // {5125316A-C3A2-475B-8507-93534DC88F15}
    const GUID IID_ITimelineProperties = {
        0x5125316A, 0xC3A2, 0x475B, {0x85, 0x07, 0x93, 0x53, 0x4D, 0xC8, 0x8F, 0x15}};
    // {DDB0472D-C911-4A1F-86D9-DC3D71A95F5A} - also in MinGW's
    // systemmediatransportcontrolsinterop.h.
    const GUID IID_ISystemMediaTransportControlsInterop = {
        0xDDB0472D, 0xC911, 0x4A1F, {0x86, 0xD9, 0xDC, 0x3D, 0x71, 0xA9, 0x5F, 0x5A}};
    // Cover art plumbing (Windows.Storage.winmd):
    // {857309DC-3FBF-4E7D-986F-EF3B1A07A964}
    // Windows.Storage.Streams.IRandomAccessStreamReferenceStatics
    const GUID IID_IRandomAccessStreamReferenceStatics = {
        0x857309DC, 0x3FBF, 0x4E7D, {0x98, 0x6F, 0xEF, 0x3B, 0x1A, 0x07, 0xA9, 0x64}};
    // {905A0FE1-BC53-11DF-8C49-001E4FC686DA} Windows.Storage.Streams.IRandomAccessStream
    const GUID IID_IRandomAccessStream = {
        0x905A0FE1, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
    // {71AF914D-C10F-484B-BC50-14BC623B3A27} Windows.Storage.Streams.IBufferFactory
    const GUID IID_IBufferFactory = {
        0x71AF914D, 0xC10F, 0x484B, {0xBC, 0x50, 0x14, 0xBC, 0x62, 0x3B, 0x3A, 0x27}};
    // {905A0FEF-BC53-11DF-8C49-001E4FC686DA} IBufferByteAccess - a plain COM
    // interface (not WinRT, so it is absent from the .winmd): the only way to
    // get at the bytes of an IBuffer.
    const GUID IID_IBufferByteAccess = {
        0x905A0FEF, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};

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

    // ---- Thumbnail (cover art) plumbing -----------------------------------
    // SMTC takes the album picture as a RandomAccessStreamReference, and the
    // bytes have to reach it *in memory*. Both documented shortcuts are dead
    // ends here:
    //   - StorageFile.GetFileFromPathAsync never completes on this STA - the
    //     completion is queued to the apartment and the frame loop does not
    //     pump it as we wait.
    //   - RandomAccessStreamReference.CreateFromUri only accepts ms-appx /
    //     ms-appdata / http / https. A file:// URI is NOT on that list, yet
    //     CreateFromUri still returns S_OK for one (it never touches the file),
    //     so put_Thumbnail looked like a success while the media flyout stayed
    //     blank the whole time.
    // What does work is CreateFromStream over an InMemoryRandomAccessStream:
    // copy the file in, rewind, hand SMTC the reference. Same recipe Chromium
    // uses. Interface order / IIDs read from the system metadata with
    // .workbuddy/tools/winmd_dump.py and winmd_guid.py.
    //
    // Windows.Storage.Streams.IRandomAccessStream - GetOutputStreamAt (6+3) to
    // write into, then Seek (6+5) and get_Size (6+0) to rewind and verify.
    struct IRandomAccessStreamVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* get_Size)(void*, unsigned long long*);
        HRESULT (STDMETHODCALLTYPE* put_Size)(void*, unsigned long long);
        HRESULT (STDMETHODCALLTYPE* GetInputStreamAt)(void*, unsigned long long, void**);
        HRESULT (STDMETHODCALLTYPE* GetOutputStreamAt)(void*, unsigned long long, void**);
        HRESULT (STDMETHODCALLTYPE* get_Position)(void*, unsigned long long*);
        HRESULT (STDMETHODCALLTYPE* Seek)(void*, unsigned long long);
        HRESULT (STDMETHODCALLTYPE* CloneStream)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* get_CanRead)(void*, unsigned char*);
        HRESULT (STDMETHODCALLTYPE* get_CanWrite)(void*, unsigned char*);
    };

    // Windows.Storage.Streams.IBufferFactory.
    struct IBufferFactoryVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6. Create(UINT32 capacity, IBuffer**)
        HRESULT (STDMETHODCALLTYPE* Create)(void*, unsigned, void**);
    };

    // Windows.Storage.Streams.IBuffer.
    struct IBufferVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* get_Capacity)(void*, unsigned*);
        HRESULT (STDMETHODCALLTYPE* get_Length)(void*, unsigned*);
        HRESULT (STDMETHODCALLTYPE* put_Length)(void*, unsigned);
    };

    // IBufferByteAccess (plain COM).
    struct IBufferByteAccessVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        // 3. Buffer(BYTE**)
        HRESULT (STDMETHODCALLTYPE* Buffer)(void*, unsigned char**);
    };

    // Windows.Storage.Streams.IOutputStream - only WriteAsync (6) is called.
    // The interface also inherits IClosable, whose Close ends up after its own
    // methods (slot 8), so the offsets below are unaffected.
    struct IOutputStreamVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6
        HRESULT (STDMETHODCALLTYPE* WriteAsync)(void*, void*, void**);
        HRESULT (STDMETHODCALLTYPE* FlushAsync)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* Close)(void*);
    };

    // Windows.Storage.Streams.IRandomAccessStreamReferenceStatics.
    struct IRandomAccessStreamReferenceStaticsVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
        HRESULT (STDMETHODCALLTYPE* GetIids)(void*, ULONG*, GUID**);
        HRESULT (STDMETHODCALLTYPE* GetRuntimeClassName)(void*, void**);
        HRESULT (STDMETHODCALLTYPE* GetTrustLevel)(void*, int*);
        // 6. CreateFromFile, 7. CreateFromUri, 8. CreateFromStream
        HRESULT (STDMETHODCALLTYPE* CreateFromFile)(void*, void*, void**);
        HRESULT (STDMETHODCALLTYPE* CreateFromUri)(void*, void*, void**);
        HRESULT (STDMETHODCALLTYPE* CreateFromStream)(void*, void*, void**);
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

    // The first three slots of every WinRT object: enough to QueryInterface an
    // IInspectable* that RoActivateInstance handed back (and every other vtable
    // in this file starts with the same trio, so safeRelease can use it too).
    struct IUnknownVtbl
    {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
        ULONG (STDMETHODCALLTYPE* AddRef)(void*);
        ULONG (STDMETHODCALLTYPE* Release)(void*);
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

    // GetProcAddress 还回来的永远是 FARPROC（一个笼统的 `long long (*)()`），
    // 把它 inline 重解释成真实签名会触发 -Wcast-function-type-mismatch。
    // 走 memcpy 是把一个函数指针的位模式搬进另一个同宽的函数指针 —— 没有类型
    // 谎言的语法形式，clang 也就没得警告。chartdl.cpp 里有一份同样的实现。
    template <typename Fn>
    Fn loadSymbol(HMODULE module, const char* name)
    {
        FARPROC proc = GetProcAddress(module, name);
        Fn out = nullptr;
        static_assert(sizeof(out) == sizeof(proc), "function pointer width mismatch");
        std::memcpy(&out, &proc, sizeof(out));
        return out;
    }

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
        gRoGetActivationFactory = loadSymbol<RoGetActivationFactoryFn>(gCombase, "RoGetActivationFactory");
        gRoActivateInstance = loadSymbol<RoActivateInstanceFn>(gCombase, "RoActivateInstance");
        gCreateString = loadSymbol<WindowsCreateStringFn>(gCombase, "WindowsCreateString");
        gDeleteString = loadSymbol<WindowsDeleteStringFn>(gCombase, "WindowsDeleteString");
        gGetStringRawBuffer = loadSymbol<WindowsGetStringRawBufferFn>(gCombase, "WindowsGetStringRawBuffer");
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

    // Whole file into memory. A jacket is a few hundred KB at most.
    std::vector<unsigned char> readFileBytes(const std::string& path)
    {
        std::vector<unsigned char> bytes;
        std::ifstream in(path_utf8::toPath(path), std::ios::binary);
        if (!in) {
            return bytes;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff len = in.tellg();
        in.seekg(0, std::ios::beg);
        if (len <= 0) {
            return bytes;
        }
        bytes.resize(static_cast<size_t>(len));
        in.read(reinterpret_cast<char*>(bytes.data()), len);
        if (!in) {
            bytes.clear();
        }
        return bytes;
    }

    // RandomAccessStreamReference for a picture on disk, or nullptr. `outInfo`
    // describes where it stopped, for the one-shot diagnostic in setTrack.
    void* coverStreamReference(const std::string& path, std::string& outInfo)
    {
        if (gRoGetActivationFactory == nullptr || gRoActivateInstance == nullptr || path.empty()) {
            outInfo = "no WinRT / empty path";
            return nullptr;
        }
        const std::vector<unsigned char> bytes = readFileBytes(path);
        if (bytes.empty()) {
            outInfo = "read failed";
            return nullptr;
        }

        // 1. The in-memory random access stream the bytes go into.
        HString streamClass(L"Windows.Storage.Streams.InMemoryRandomAccessStream");
        void* instance = nullptr;
        if (FAILED(gRoActivateInstance(streamClass.handle, &instance)) || instance == nullptr) {
            outInfo = "InMemoryRandomAccessStream activation failed";
            return nullptr;
        }
        void* stream = nullptr;
        if (FAILED(vt<IUnknownVtbl>(instance)->QueryInterface(instance, &IID_IRandomAccessStream, &stream))
            || stream == nullptr) {
            safeRelease(instance);
            outInfo = "IRandomAccessStream QI failed";
            return nullptr;
        }
        safeRelease(instance);

        // 2. The bytes go into an IBuffer. WinRT gives no direct way to fill
        //    one: the pointer comes from IBufferByteAccess, a plain COM
        //    interface every IBuffer implements. (The obvious alternative -
        //    DataWriter::WriteBytes - was tried first and dropped: releasing the
        //    writer closes the stream it wrapped and DetachStream takes an owned
        //    reference back, so the stream kept dying under us.)
        HString bufferClass(L"Windows.Storage.Streams.Buffer");
        void* bufferFactory = nullptr;
        if (FAILED(gRoGetActivationFactory(bufferClass.handle, &IID_IBufferFactory, &bufferFactory))
            || bufferFactory == nullptr) {
            safeRelease(stream);
            outInfo = "Buffer factory unavailable";
            return nullptr;
        }
        void* buffer = nullptr;
        const HRESULT bufferHr = vt<IBufferFactoryVtbl>(bufferFactory)
                                     ->Create(bufferFactory, static_cast<unsigned>(bytes.size()), &buffer);
        safeRelease(bufferFactory);
        if (FAILED(bufferHr) || buffer == nullptr) {
            safeRelease(stream);
            outInfo = "Create buffer failed";
            return nullptr;
        }
        {
            void* byteAccess = nullptr;
            if (FAILED(vt<IUnknownVtbl>(buffer)->QueryInterface(buffer, &IID_IBufferByteAccess, &byteAccess))
                || byteAccess == nullptr) {
                safeRelease(buffer);
                safeRelease(stream);
                outInfo = "IBufferByteAccess QI failed";
                return nullptr;
            }
            unsigned char* raw = nullptr;
            if (SUCCEEDED(vt<IBufferByteAccessVtbl>(byteAccess)->Buffer(byteAccess, &raw)) && raw != nullptr) {
                std::memcpy(raw, bytes.data(), bytes.size());
                vt<IBufferVtbl>(buffer)->put_Length(buffer, static_cast<unsigned>(bytes.size()));
            }
            safeRelease(byteAccess);
        }

        // 3. Write it into the stream.
        void* output = nullptr;
        if (FAILED(vt<IRandomAccessStreamVtbl>(stream)->GetOutputStreamAt(stream, 0, &output))
            || output == nullptr) {
            safeRelease(buffer);
            safeRelease(stream);
            outInfo = "GetOutputStreamAt failed";
            return nullptr;
        }
        // The operation this returns is deliberately never touched - not
        // waited on, not cancelled, not released. InMemoryRandomAccessStream
        // copies the bytes inline (the stream's own size, checked below, is the
        // proof), yet its IAsyncOperation stays in AsyncStatus::Started forever
        // here - a message pump does not move it, and both Cancel and Release
        // fault on it (measured: two different crashes). Leaving it alone costs
        // one small object per track change, which is the cheap way out.
        void* writeOp = nullptr;
        HRESULT hr = vt<IOutputStreamVtbl>(output)->WriteAsync(output, buffer, &writeOp);
        (void)writeOp;

        // 4. Rewind, and check the length actually landed: a write that silently
        //    goes nowhere is exactly the failure mode this whole path replaced.
        auto* streamV = vt<IRandomAccessStreamVtbl>(stream);
        streamV->Seek(stream, 0);
        unsigned long long size = 0;
        streamV->get_Size(stream, &size);
        if (SUCCEEDED(hr) && size != bytes.size()) {
            hr = E_FAIL;
        }
        if (FAILED(hr)) {
            safeRelease(output);
            safeRelease(buffer);
            safeRelease(stream);
            outInfo = "write failed";
            return nullptr;
        }

        // 5. Wrap it for SMTC.
        HString refClassId(L"Windows.Storage.Streams.RandomAccessStreamReference");
        void* refFactory = nullptr;
        if (FAILED(gRoGetActivationFactory(refClassId.handle, &IID_IRandomAccessStreamReferenceStatics, &refFactory))
            || refFactory == nullptr) {
            safeRelease(output);
            safeRelease(buffer);
            safeRelease(stream);
            outInfo = "reference factory unavailable";
            return nullptr;
        }
        void* reference = nullptr;
        const HRESULT refHr = vt<IRandomAccessStreamReferenceStaticsVtbl>(refFactory)
                                  ->CreateFromStream(refFactory, stream, &reference);
        safeRelease(refFactory);
        safeRelease(output);
        safeRelease(buffer);
        safeRelease(stream);

        outInfo = std::to_string(bytes.size()) + " bytes in, stream size " + std::to_string(size);
        if (FAILED(refHr) || reference == nullptr) {
            outInfo += " (CreateFromStream failed)";
            return nullptr;
        }
        return reference;
    }

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
    if (mThumbnail != nullptr) {
        safeRelease(mThumbnail);
        mThumbnail = nullptr;
    }
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

void SystemMedia::setTrack(const std::string& title, const std::string& artist, double durationSec,
    const std::string& coverPath)
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

    // Cover art: the bytes are copied into an in-memory stream (the plumbing
    // above explains why neither a file:// URI nor StorageFile works).
    if (!coverPath.empty()) {
        std::string info;
        if (mThumbnail != nullptr) {
            safeRelease(mThumbnail);
            mThumbnail = nullptr;
        }
        mThumbnail = coverStreamReference(coverPath, info);
        if (mThumbnail != nullptr) {
            diag("put_Thumbnail", vt<IDisplayUpdaterVtbl>(updater)->put_Thumbnail(updater, mThumbnail));
        } else {
            diag("put_Thumbnail SKIPPED (no stream reference)", E_FAIL);
        }
        if (!s_metaDiagLogged) {
            std::printf("[media] cover -> %s (%s; %s)\n", mThumbnail == nullptr ? "FAILED" : "ok",
                coverPath.c_str(), info.c_str());
            std::fflush(stdout);
        }
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

void SystemMedia::setReporting(bool on)
{
#ifdef _WIN32
    if (mSmtc == nullptr) {
        return;
    }
    auto* v = vt<ISMTCVtbl>(mSmtc);
    if (on) {
        v->put_IsEnabled(mSmtc, 1);
    } else {
        v->put_PlaybackStatus(mSmtc, StatusStopped);
        v->put_IsEnabled(mSmtc, 0);
        if (mThumbnail != nullptr) {
            safeRelease(mThumbnail);
            mThumbnail = nullptr;
        }
        mTrackTitle.clear();
        mTrackArtist.clear();
    }
    // Read it back: a toggle that silently no-ops looks exactly like Windows
    // ignoring us (the same reason setTrack reads its own properties back).
    unsigned char enabled = 7;
    v->get_IsEnabled(mSmtc, &enabled);
    std::printf("[media] reporting %s (IsEnabled readback = %d)\n", on ? "on" : "off",
        static_cast<int>(enabled));
    std::fflush(stdout);
    // Whatever the cached state says, the shell has just been reset: force the
    // next setTrack / updatePlayback to push everything again.
    mLastPositionSec = -1.0e9;
    mLastStatus = -1;
    mLastDurationSec = 0.0;
#else
    (void)on;
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
