#include "ShellIntegration.hpp"

#ifdef _WIN32
#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace platform::shell
{
namespace
{
// ---------------------------------------------------------------------------
// .sus 文件关联用到的常量
// ---------------------------------------------------------------------------
constexpr const wchar_t* kProgId = L"CppSekai.Chart";
constexpr const wchar_t* kProgIdLabel = L"SUS 谱面 (CppSekai)";

void log(const char* what, bool ok, long code)
{
    std::printf("[shell] %s %s%s\n", what, ok ? "ok" : "FAILED",
        ok ? "" : (" (code=" + std::to_string(code) + ")").c_str());
    std::fflush(stdout);
}

bool writeString(HKEY root, const wchar_t* subKey, const wchar_t* value, const std::wstring& data)
{
    HKEY key = nullptr;
    const long created = RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) {
        log("RegCreateKeyEx", false, created);
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    const long written = RegSetValueExW(key, value, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(data.c_str()), bytes);
    RegCloseKey(key);
    if (written != ERROR_SUCCESS) {
        log("RegSetValueEx", false, written);
        return false;
    }
    return true;
}

// 读一个 REG_SZ（不存在 / 类型不对都当空）。
std::wstring readString(const wchar_t* subKey, const wchar_t* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::wstring();
    }
    wchar_t buffer[1024] = {};
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    const long status = RegQueryValueExW(key, value, nullptr, &type,
        reinterpret_cast<BYTE*>(buffer), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return std::wstring();
    }
    buffer[(sizeof(buffer) / sizeof(buffer[0])) - 1] = L'\0';
    return std::wstring(buffer);
}

// ---------------------------------------------------------------------------
// Jump List：手写的 COM。
//
// 这个工具链里没有 Windows SDK 的导入库（头文件倒是有），而我们只要这一小撮接口 ——
// 手写 vtable 比把整个 shobjidl.h 拉进来省心。但要记住 COM 的 vtable 是**位置寻址**
// 的：每个槽位都按声明顺序列全，用不到的也留着占位，少一个后面全错位。
//
// 四个接口：ICustomDestinationList（把列表交给 shell）、IObjectArray（下面自己实现
// 一个，只为了把 IShellLink 交出去）、IShellLinkW（一条跳转项）、IPropertyStore
// （设显示名 —— 不设的话列表里显示的是 exe 路径那一串）。
//
// 这些定义全放在匿名命名空间的**文件作用域**，不能塞进函数里：下面 IObjectArray 那
// 几个方法是**无捕获** lambda（要能转成函数指针），看不见函数的局部变量。
// ---------------------------------------------------------------------------
struct ICustomDestinationListVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(void*);
    ULONG (STDMETHODCALLTYPE* Release)(void*);
    HRESULT (STDMETHODCALLTYPE* SetAppID)(void*, const wchar_t*);
    HRESULT (STDMETHODCALLTYPE* BeginList)(void*, unsigned*, const GUID&, void**);
    HRESULT (STDMETHODCALLTYPE* AppendCategory)(void*, const wchar_t*, void*);
    HRESULT (STDMETHODCALLTYPE* AppendKnownCategory)(void*, int);
    HRESULT (STDMETHODCALLTYPE* AddUserTasks)(void*, void*);
    HRESULT (STDMETHODCALLTYPE* CommitList)(void*);
    HRESULT (STDMETHODCALLTYPE* DeleteList)(void*);
    HRESULT (STDMETHODCALLTYPE* AbortList)(void*);
};

struct IObjectArrayVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(void*);
    ULONG (STDMETHODCALLTYPE* Release)(void*);
    HRESULT (STDMETHODCALLTYPE* GetCount)(void*, unsigned*);
    HRESULT (STDMETHODCALLTYPE* GetAt)(void*, unsigned, const GUID&, void**);
};

struct IShellLinkWVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(void*);
    ULONG (STDMETHODCALLTYPE* Release)(void*);
    HRESULT (STDMETHODCALLTYPE* GetPath)(void*, wchar_t*, int, void*, unsigned);
    HRESULT (STDMETHODCALLTYPE* GetIDList)(void*, void**);
    HRESULT (STDMETHODCALLTYPE* SetIDList)(void*, void*);
    HRESULT (STDMETHODCALLTYPE* GetDescription)(void*, wchar_t*, int);
    HRESULT (STDMETHODCALLTYPE* SetDescription)(void*, const wchar_t*);
    HRESULT (STDMETHODCALLTYPE* GetWorkingDirectory)(void*, wchar_t*, int);
    HRESULT (STDMETHODCALLTYPE* SetWorkingDirectory)(void*, const wchar_t*);
    HRESULT (STDMETHODCALLTYPE* GetArguments)(void*, wchar_t*, int);
    HRESULT (STDMETHODCALLTYPE* SetArguments)(void*, const wchar_t*);
    HRESULT (STDMETHODCALLTYPE* GetHotkey)(void*, unsigned short*);
    HRESULT (STDMETHODCALLTYPE* SetHotkey)(void*, unsigned short);
    HRESULT (STDMETHODCALLTYPE* GetShowCmd)(void*, int*);
    HRESULT (STDMETHODCALLTYPE* SetShowCmd)(void*, int);
    HRESULT (STDMETHODCALLTYPE* GetIconLocation)(void*, wchar_t*, int, int*);
    HRESULT (STDMETHODCALLTYPE* SetIconLocation)(void*, const wchar_t*, int);
    HRESULT (STDMETHODCALLTYPE* SetRelativePath)(void*, const wchar_t*, unsigned long);
    HRESULT (STDMETHODCALLTYPE* Resolve)(void*, HWND, unsigned long);
    HRESULT (STDMETHODCALLTYPE* SetPath)(void*, const wchar_t*);
};

struct IPropertyStoreVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, const GUID*, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(void*);
    ULONG (STDMETHODCALLTYPE* Release)(void*);
    HRESULT (STDMETHODCALLTYPE* GetCount)(void*, unsigned long*);
    HRESULT (STDMETHODCALLTYPE* GetAt)(void*, unsigned long, void*);
    HRESULT (STDMETHODCALLTYPE* GetValue)(void*, const void*, void*);
    HRESULT (STDMETHODCALLTYPE* SetValue)(void*, const void*, const void*);
    HRESULT (STDMETHODCALLTYPE* Commit)(void*);
};

// GUID 全部对着工具链里 shobjidl.h / objectarray.h / propkey.h 抄的，别凭记忆改。
const GUID kClsidDestinationList = {
    0x77f10cf0, 0x3db5, 0x4966, {0xb5, 0x20, 0xb7, 0xc5, 0x4f, 0xd3, 0x5e, 0xd6}};
const GUID kIidCustomDestinationList = {
    0x6332debf, 0x87b5, 0x4670, {0x90, 0xc0, 0x5e, 0x57, 0xb4, 0x08, 0xa4, 0x9e}};
const GUID kIidObjectArray = {
    0x92ca9dcd, 0x5622, 0x4bba, {0xa8, 0x05, 0x5e, 0x9f, 0x54, 0x1b, 0xd8, 0xc9}};
const GUID kClsidShellLink = {
    0x00021401, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidShellLinkW = {
    0x000214f9, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kIidPropertyStore = {
    0x886d8eeb, 0x8cf2, 0x4446, {0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99}};
// PKEY_Title {F29F85E0-4FF9-1068-AB91-08002B27B3D9}, pid 2
const GUID kPkeyTitleFmtid = {
    0xf29f85e0, 0x4ff9, 0x1068, {0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9}};

// PROPERTYKEY / PROPVARIANT 的 ABI：前者 24 字节（GUID + DWORD + 填充），后者 24
// 字节（vt 加 3 个保留字共 8，联合体要 8 字节对齐所以从偏移 8 开始）。VT_LPWSTR
// 那个指针就落在偏移 8 上。
struct PropertyKey
{
    GUID fmtid;
    unsigned long pid;
};
struct PropVariant
{
    unsigned short vt;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned short reserved3;
    void* value;
    unsigned long long tail;
};
constexpr unsigned short kVarTypeLpWStr = 31;
const PropertyKey kPkeyTitle{kPkeyTitleFmtid, 2};

// 自己的 IObjectArray：只实现 GetCount / GetAt，把攒好的 IShellLink 交出去。
// 用 shell32 的 EnumerableObjectCollection 也行，但那又是另一个 CLSID 加一整套
// IObjectCollection 的 vtable，不如这两个方法自己写。
struct LinkArray
{
    const IObjectArrayVtbl* vtable;
    unsigned long refCount;
    const std::vector<void*>* links;
};

// 取 obj 的 vtable 第 0 项（QueryInterface）调一下：里面的对象本来就是
// IShellLinkW，直接转给它最省事。
HRESULT queryInterfaceOn(void* obj, const GUID& riid, void** out)
{
    using QueryFn = HRESULT (STDMETHODCALLTYPE*)(void*, const GUID*, void**);
    void** vtable = *reinterpret_cast<void***>(obj);
    auto query = reinterpret_cast<QueryFn>(vtable[0]);
    return query(obj, &riid, out);
}

const IObjectArrayVtbl kLinkArrayVtbl = {
    [](void* self, const GUID* riid, void** out) -> HRESULT {
        if (out == nullptr || riid == nullptr) {
            return static_cast<HRESULT>(0x80004003u); // E_POINTER
        }
        if (IsEqualGUID(*riid, kIidObjectArray) || IsEqualGUID(*riid, IID_IUnknown)) {
            *out = self;
            ++static_cast<LinkArray*>(self)->refCount;
            return 0;
        }
        *out = nullptr;
        return static_cast<HRESULT>(0x80004002u); // E_NOINTERFACE
    },
    [](void* self) -> ULONG { return ++static_cast<LinkArray*>(self)->refCount; },
    [](void* self) -> ULONG {
        auto* array = static_cast<LinkArray*>(self);
        return array->refCount == 0 ? 0 : --array->refCount;
    },
    [](void* self, unsigned* count) -> HRESULT {
        if (count == nullptr) {
            return static_cast<HRESULT>(0x80004003u);
        }
        *count = static_cast<unsigned>(static_cast<LinkArray*>(self)->links->size());
        return 0;
    },
    [](void* self, unsigned index, const GUID& riid, void** out) -> HRESULT {
        const auto* links = static_cast<LinkArray*>(self)->links;
        if (out == nullptr) {
            return static_cast<HRESULT>(0x80004003u);
        }
        if (index >= links->size()) {
            *out = nullptr;
            return static_cast<HRESULT>(0x80070057u); // E_INVALIDARG
        }
        return queryInterfaceOn((*links)[index], riid, out);
    }};
} // namespace

bool associateSus(const std::wstring& exePath)
{
    const std::wstring command = L"\"" + exePath + L"\" --sus \"%1\"";
    const std::wstring icon = exePath + L",0";
    // 顺序无所谓，但 ProgID 那几项必须齐 —— 少了 command，Explorer 会拿"打开方式"
    // 去问用户，体验比不关联还差。
    if (!writeString(HKEY_CURRENT_USER, L"Software\\Classes\\CppSekai.Chart", nullptr, kProgIdLabel)) {
        return false;
    }
    if (!writeString(HKEY_CURRENT_USER, L"Software\\Classes\\CppSekai.Chart\\DefaultIcon", nullptr, icon)) {
        return false;
    }
    if (!writeString(HKEY_CURRENT_USER, L"Software\\Classes\\CppSekai.Chart\\shell\\open\\command",
            nullptr, command)) {
        return false;
    }
    if (!writeString(HKEY_CURRENT_USER, L"Software\\Classes\\.sus", nullptr, kProgId)) {
        return false;
    }
    std::printf("[shell] .sus -> %ls  (HKCU\\Software\\Classes, 双击即 --sus <文件>)\n", kProgId);
    std::printf("[shell]   command = %ls\n", command.c_str());
    std::fflush(stdout);
    return true;
}

bool disassociateSus()
{
    // 只在 .sus 现在还指着我们的时候才动它 —— 用户自己改过关联（或者装了别的
    // 谱面工具）就不能替他拆掉。
    const std::wstring current = readString(L"Software\\Classes\\.sus", nullptr);
    if (!current.empty() && current != kProgId) {
        std::printf("[shell] .sus 现在指向 %ls（不是本程序），不动它\n", current.c_str());
        std::fflush(stdout);
        return false;
    }
    const long removedExt = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.sus");
    const long removedProg = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\CppSekai.Chart");
    const bool ok = removedExt == ERROR_SUCCESS || removedExt == ERROR_FILE_NOT_FOUND;
    if (ok) {
        // 这两个返回码 0 = ERROR_SUCCESS，"没找到"也是成功（本来就没装）。别把它
        // 打成 `删除: 0` —— 读着像失败。
        std::printf("[shell] .sus 关联已撤销（.sus=%s, ProgID=%s）\n",
            removedExt == ERROR_SUCCESS ? "removed" : "was absent",
            removedProg == ERROR_SUCCESS ? "removed" : "was absent");
        std::fflush(stdout);
    } else {
        log("RegDeleteTree(.sus)", false, removedExt);
    }
    return ok;
}

bool susAssociated()
{
    return readString(L"Software\\Classes\\.sus", nullptr) == kProgId;
}

bool rebuildJumpList(const std::wstring& exePath,
    const std::vector<std::pair<std::wstring, std::vector<JumpEntry>>>& categories)
{
    // COM 得先起来。SystemMedia::init 那边 CoInitializeEx 过，但 shell 也可能被单独
    // 调用，所以自己兜一次底：S_FALSE / RPC_E_CHANGED_MODE 都算"能用"。
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    void* destinationList = nullptr;
    HRESULT hr = CoCreateInstance(kClsidDestinationList, nullptr, CLSCTX_INPROC_SERVER,
        kIidCustomDestinationList, &destinationList);
    if (FAILED(hr) || destinationList == nullptr) {
        log("CoCreateInstance(ICustomDestinationList)", false, static_cast<long>(hr));
        return false;
    }
    const auto* dl = reinterpret_cast<const ICustomDestinationListVtbl*>(
        *reinterpret_cast<void**>(destinationList));

    unsigned minSlots = 0;
    void* removed = nullptr;
    hr = dl->BeginList(destinationList, &minSlots, kIidObjectArray, &removed);
    // BeginList 交出来的是「用户自己从跳转列表里删掉的项」。
    //
    // **必须尊重它**：我们是每次整份重建（AppendCategory 是追加语义，旧的不会
    // 自动清掉，所以只能全量喂一遍），如果不把这几个记下来又原样加回去，
    // 用户右键「从列表中删除」删一次、下次启动它又冒出来 —— 删了等于没删。
    // 记的是**参数串**（命令行的 `--sus "路径"`），比抠路径出来比对稳。
    std::vector<std::wstring> userRemoved;
    if (removed != nullptr) {
        const auto* removedArray = reinterpret_cast<const IObjectArrayVtbl*>(
            *reinterpret_cast<void**>(removed));
        unsigned removedCount = 0;
        if (SUCCEEDED(removedArray->GetCount(removed, &removedCount))) {
            for (unsigned i = 0; i < removedCount; ++i) {
                void* item = nullptr;
                if (FAILED(removedArray->GetAt(removed, i, kIidShellLinkW, &item))
                    || item == nullptr) {
                    continue;
                }
                const auto* removedLink = reinterpret_cast<const IShellLinkWVtbl*>(
                    *reinterpret_cast<void**>(item));
                wchar_t removedArgs[1024] = {};
                if (SUCCEEDED(removedLink->GetArguments(item, removedArgs, 1024))) {
                    userRemoved.emplace_back(removedArgs);
                }
                removedLink->Release(item);
            }
        }
        // BeginList 给的已经是我们请求的接口（IObjectArray），直接 Release。
        removedArray->Release(removed);
        removed = nullptr;
        if (!userRemoved.empty()) {
            std::printf("[shell] jump list: %d item(s) removed by the user - keeping them out\n",
                static_cast<int>(userRemoved.size()));
            std::fflush(stdout);
        }
    }
    if (FAILED(hr)) {
        log("ICustomDestinationList::BeginList", false, static_cast<long>(hr));
        dl->Release(destinationList);
        return false;
    }

    int addedLinks = 0;
    for (const auto& category : categories) {
        std::vector<void*> links;
        for (const JumpEntry& entry : category.second) {
            // 路径已经没了的（换了 charts 目录、曲子删了）就跳过：跳转项点了打不开
            // 比不显示更糟。
            if (entry.susPath.empty()
                || GetFileAttributesW(entry.susPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                continue;
            }
            void* link = nullptr;
            if (FAILED(CoCreateInstance(kClsidShellLink, nullptr, CLSCTX_INPROC_SERVER,
                    kIidShellLinkW, &link))) {
                continue;
            }
            const auto* shellLink = reinterpret_cast<const IShellLinkWVtbl*>(
                *reinterpret_cast<void**>(link));
            shellLink->SetPath(link, exePath.c_str());
            const std::wstring arguments = L"--sus \"" + entry.susPath + L"\"";
            // 用户删过的那几项不再加回去（见上面 userRemoved）。
            if (std::find(userRemoved.begin(), userRemoved.end(), arguments)
                != userRemoved.end()) {
                shellLink->Release(link);
                continue;
            }
            shellLink->SetArguments(link, arguments.c_str());
            shellLink->SetIconLocation(link, exePath.c_str(), 0);
            if (!entry.detail.empty()) {
                shellLink->SetDescription(link, entry.detail.c_str());
            }
            // 显示名：不给的话列表里显示的是 exe 路径。属性存储那一套只有
            // PKEY_Title 一项，失败就退回"没设"（至少链接还能用）。
            void* store = nullptr;
            if (SUCCEEDED(shellLink->QueryInterface(link, &kIidPropertyStore, &store))
                && store != nullptr) {
                const auto* props = reinterpret_cast<const IPropertyStoreVtbl*>(
                    *reinterpret_cast<void**>(store));
                PropVariant title{};
                title.vt = kVarTypeLpWStr;
                title.value = const_cast<wchar_t*>(entry.title.c_str());
                if (SUCCEEDED(props->SetValue(store, &kPkeyTitle, &title))) {
                    props->Commit(store);
                }
                props->Release(store);
            }
            links.push_back(link);
        }
        if (links.empty()) {
            continue;
        }
        LinkArray array{&kLinkArrayVtbl, 1, &links};
        hr = dl->AppendCategory(destinationList, category.first.c_str(), &array);
        if (FAILED(hr)) {
            log("ICustomDestinationList::AppendCategory", false, static_cast<long>(hr));
        } else {
            addedLinks += static_cast<int>(links.size());
        }
        for (void* link : links) {
            const auto* shellLink = reinterpret_cast<const IShellLinkWVtbl*>(
                *reinterpret_cast<void**>(link));
            shellLink->Release(link);
        }
    }

    hr = dl->CommitList(destinationList);
    if (FAILED(hr)) {
        // CommitList 失败之后那份列表是废的，按文档要 AbortList（不然下一次
        // BeginList 会一直报"上一次的事务没结束"）。
        dl->AbortList(destinationList);
        log("ICustomDestinationList::CommitList", false, static_cast<long>(hr));
        dl->Release(destinationList);
        return false;
    }
    dl->Release(destinationList);
    std::printf("[shell] jump list rebuilt: %d item(s) in %d category(ies), minslots=%u\n",
        addedLinks, static_cast<int>(categories.size()), minSlots);
    std::fflush(stdout);
    return true;
}

} // namespace platform::shell
#endif
