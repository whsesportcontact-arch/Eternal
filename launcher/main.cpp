// Enta Launcher
// Shows your skin in 3D, lets you switch mods on/off before starting, keeps itself and Enta.dll up to date
// from this repo's GitHub releases, then starts Minecraft Bedrock (Windows) and loads Enta.dll into it.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <commdlg.h>
#include <aclapi.h>
#include <sddl.h>
#include <urlmon.h>
#include <wininet.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "skin_render.h"

#ifndef ENTA_REPO
#define ENTA_REPO L""
#endif
#ifndef ENTA_BUILD_ID
#define ENTA_BUILD_ID L""
#endif
#ifndef ENTA_CHANNEL
#define ENTA_CHANNEL L"latest"
#endif

namespace {

constexpr wchar_t kGameExe[] = L"Minecraft.Windows.exe";
constexpr wchar_t kDllName[] = L"Enta.dll";

// ------------------------------------------------------------------------------------------------ theme

constexpr COLORREF kBg = RGB(0x08, 0x05, 0x0E);
constexpr COLORREF kPanel = RGB(0x12, 0x0B, 0x1D);
constexpr COLORREF kPanelHover = RGB(0x1C, 0x12, 0x2C);
constexpr COLORREF kPurple = RGB(0x8B, 0x3D, 0xFF);
constexpr COLORREF kPurpleHot = RGB(0xA6, 0x6B, 0xFF);
constexpr COLORREF kPurpleDim = RGB(0x3B, 0x2E, 0x4D);
constexpr COLORREF kOff = RGB(0x2A, 0x22, 0x36);
constexpr COLORREF kText = RGB(0xF2, 0xEE, 0xFF);
constexpr COLORREF kSubText = RGB(0x9C, 0x8F, 0xB5);
constexpr COLORREF kWarn = RGB(0xFF, 0xC8, 0x6B);

constexpr int kW = 780, kH = 488;

const RECT kClose = { kW - 42, 14, kW - 14, 42 };
const RECT kMin = { kW - 76, 14, kW - 48, 42 };
const RECT kSkinPanel = { 20, 100, 282, 384 };
const RECT kViewer = { 31, 128, 271, 298 };
const RECT kPrev = { 31, 302, 55, 326 };
const RECT kNext = { 247, 302, 271, 326 };
const RECT kSkinName = { 57, 300, 245, 318 };
const RECT kSkinNote = { 31, 318, 271, 336 };
const RECT kAddSkin = { 31, 342, 117, 374 };
const RECT kUseSkin = { 123, 342, 271, 374 };
const RECT kRemoveSkin = { 196, 106, 270, 126 };
const RECT kModsPanel = { 298, 100, 760, 384 };
const RECT kList = { 306, 160, 752, 378 };
const RECT kLaunch = { 470, 400, 760, 452 };
const RECT kStatus = { 470, 456, 760, 480 };
const RECT kChannel = { 20, 404, 230, 428 };
const RECT kVersion = { 20, 432, 460, 480 };
constexpr int kRowH = 34;

// ------------------------------------------------------------------------------------------------ state

#define WM_APP_REFRESH (WM_APP + 1)
#define WM_APP_QUIT (WM_APP + 2)
#define TIMER_ANIM 1

HWND gWnd = nullptr;
HFONT gTitleFont, gBodyFont, gSmallFont, gButtonFont, gHeadFont;

std::mutex gLock; // guards the strings below (written by worker threads)
std::wstring gStatus = L"Ready.";
std::wstring gUpdateText = L"Checking for updates...";
bool gUpdateWarn = false;

std::atomic_bool gBusy = false;         // launching
std::atomic_bool gUpdating = false;     // update check running
std::atomic_bool gGameHasEnta = false;  // Minecraft running with Enta loaded
bool gHoverLaunch = false, gHoverClose = false, gHoverMin = false, gHoverChannel = false;
enum SkinButton { SB_None, SB_Prev, SB_Next, SB_Add, SB_Use, SB_Remove };
int gHoverSkin = SB_None;
int gHoverRow = -1;

std::atomic_bool gDevChannel = false;
bool gJustUpdated = false; // started by a self-update: don't try again this run

// skin viewer
// skin library: the skin you have in game (exported by Enta) + skins you added here
struct SkinEntry {
    std::wstring path;
    std::wstring label;
    bool fromGame = false;
};
std::vector<SkinEntry> gSkins;
std::wstring gSelectedSkin; // path; remembered in launcher.ini
skin::Image gSkin;          // the selected one, decoded
std::wstring gLoadedSkinPath;
FILETIME gSkinTime {};
float gYaw = 25.f;
bool gDragging = false;
int gDragX = 0;
DWORD gLastInput = 0;
HBITMAP gViewerBmp = nullptr;
void* gViewerBits = nullptr;

// mods
struct Mod {
    std::string name;
    std::wstring display;
    int category = 0;
    bool savedOn = false; // what the game has
    bool on = false;      // what you picked here
};
std::vector<Mod> gMods;
FILETIME gModsTime {};
int gScroll = 0;

void refresh() { PostMessageW(gWnd, WM_APP_REFRESH, 0, 0); }

void setStatus(std::wstring const& s) {
    {
        std::lock_guard g(gLock);
        gStatus = s;
    }
    refresh();
}

void setUpdateText(std::wstring const& s, bool warn = false) {
    {
        std::lock_guard g(gLock);
        gUpdateText = s;
        gUpdateWarn = warn;
    }
    refresh();
}

// ------------------------------------------------------------------------------------------------ files

std::wstring exePath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::wstring exeDir() {
    std::wstring p = exePath();
    return p.substr(0, p.find_last_of(L"\\/"));
}

bool fileExists(std::wstring const& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// %LOCALAPPDATA%\Enta\Launcher, shared with Enta.dll
std::wstring dataDir() {
    wchar_t buf[MAX_PATH] {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : exeDir();
    CreateDirectoryW((base + L"\\Enta").c_str(), nullptr);
    std::wstring dir = base + L"\\Enta\\Launcher";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool readFile(std::wstring const& p, std::string& out) {
    std::ifstream f(std::filesystem::path(p), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(std::wstring const& p, std::string const& data) {
    std::ofstream f(std::filesystem::path(p), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\xEF' || s[i] == '\xBB' || s[i] == '\xBF')) i++;
    return s.substr(i);
}

std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring const& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool fileTime(std::wstring const& p, FILETIME& ft) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return false;
    ft = d.ftLastWriteTime;
    return true;
}

bool sameTime(FILETIME a, FILETIME b) { return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime; }

// ------------------------------------------------------------------------------------------------ settings

std::wstring settingsPath() { return dataDir() + L"\\launcher.ini"; }

void loadSettings() {
    // first run: use the channel this launcher was downloaded from
    gDevChannel = std::wstring(ENTA_CHANNEL) == L"dev";
    std::string text;
    if (!readFile(settingsPath(), text)) return;
    std::istringstream ss(text);
    for (std::string line; std::getline(ss, line);) {
        line = trim(line);
        if (line == "channel=dev") gDevChannel = true;
        if (line == "channel=stable") gDevChannel = false;
        if (line.rfind("skin=", 0) == 0) gSelectedSkin = widen(line.substr(5));
    }
}

void saveSettings() {
    writeFile(settingsPath(), std::string("channel=") + (gDevChannel.load() ? "dev" : "stable") + "\nskin=" +
                                  narrow(gSelectedSkin) + "\n");
}

// ------------------------------------------------------------------------------------------------ game + injection

DWORD findGame() {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe {};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, kGameExe) == 0) {
            pid = pe.th32ProcessID;
            break;
        }
    }
    CloseHandle(snap);
    return pid;
}

bool alreadyLoaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me {};
    me.dwSize = sizeof(me);
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        if (_wcsicmp(me.szModule, kDllName) == 0) {
            found = true;
            break;
        }
    }
    CloseHandle(snap);
    return found;
}

// Older (UWP/Store) builds of Minecraft run sandboxed and can only read files that
// "ALL APPLICATION PACKAGES" has access to. Harmless on newer builds.
void grantAppContainerRead(std::wstring const& path) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) return;
    PACL oldDacl = nullptr, newDacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl,
                              nullptr, &sd) == ERROR_SUCCESS) {
        EXPLICIT_ACCESSW ea {};
        ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        if (SetEntriesInAclW(1, &ea, oldDacl, &newDacl) == ERROR_SUCCESS) {
            SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                  nullptr, newDacl, nullptr);
            LocalFree(newDacl);
        }
        LocalFree(sd);
    }
    LocalFree(sid);
}

bool loadInto(DWORD pid, std::wstring const& dll, std::wstring& err) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                  PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) {
        err = L"Couldn't open Minecraft (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    if (remote && WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
        HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
        if (th) {
            WaitForSingleObject(th, 15000);
            DWORD code = 0;
            GetExitCodeThread(th, &code);
            ok = code != 0; // LoadLibrary returned a module handle
            if (!ok) err = L"Minecraft refused to load Enta.dll (wrong game version?).";
            CloseHandle(th);
        } else {
            err = L"Couldn't start loader thread (error " + std::to_wstring(GetLastError()) + L").";
        }
    } else {
        err = L"Couldn't write into Minecraft's memory.";
    }
    if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return ok;
}

// ------------------------------------------------------------------------------------------------ updates

std::wstring channelTag() { return gDevChannel ? L"dev" : L"latest"; }

std::wstring releaseUrl(std::wstring const& file) {
    return L"https://github.com/" + std::wstring(ENTA_REPO) + L"/releases/download/" + channelTag() + L"/" + file;
}

bool download(std::wstring const& url, std::wstring const& to) {
    DeleteUrlCacheEntryW(url.c_str());
    DeleteFileW(to.c_str());
    return SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), to.c_str(), 0, nullptr)) && fileExists(to);
}

std::wstring dllPath() { return exeDir() + L"\\" + kDllName; }
std::wstring dllVersionPath() { return dataDir() + L"\\dll_version.txt"; }
std::wstring pendingDllPath() { return dllPath() + L".download"; }

std::string installedDllVersion() {
    std::string v;
    readFile(dllVersionPath(), v);
    return trim(v);
}

// Swap in a downloaded DLL. Fails while Minecraft has the old one loaded; then it waits for next time.
bool installPendingDll(std::string const& version) {
    if (!fileExists(pendingDllPath())) return false;
    if (!MoveFileExW(pendingDllPath().c_str(), dllPath().c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    if (!version.empty()) writeFile(dllVersionPath(), version + "\n");
    return true;
}

std::string gPendingVersion; // version of a downloaded DLL waiting to be swapped in (guarded by gLock)

void setPendingVersion(std::string const& v) {
    std::lock_guard g(gLock);
    gPendingVersion = v;
}

std::string pendingVersion() {
    std::lock_guard g(gLock);
    return gPendingVersion;
}

// Replace this exe with the newest one and restart. Returns false if it couldn't.
bool selfUpdate() {
    const std::wstring self = exePath();
    const std::wstring dir = exeDir();
    const std::wstring fresh = dir + L"\\EntaLauncher.new.exe";
    const std::wstring old = dir + L"\\EntaLauncher.old.exe";

    if (!download(releaseUrl(L"EntaLauncher.exe"), fresh)) return false;
    DeleteFileW(old.c_str());
    // Windows lets you rename a running exe, just not overwrite it
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(fresh.c_str());
        return false;
    }
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING); // put it back
        return false;
    }

    std::wstring cmd = L"\"" + self + L"\" --updated";
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    if (CreateProcessW(self.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        PostMessageW(gWnd, WM_APP_QUIT, 0, 0);
        return true;
    }
    return false;
}

void updateWorker() {
    const std::wstring repo = ENTA_REPO;
    const std::string myBuild = narrow(ENTA_BUILD_ID);
    const std::wstring channelName = gDevChannel ? L"test builds" : L"stable";

    if (repo.empty()) {
        setUpdateText(L"Local build: automatic updates are off.");
        gUpdating = false;
        return;
    }

    setUpdateText(L"Checking for updates (" + channelName + L")...");
    std::wstring versionFile = dataDir() + L"\\version.download.txt";
    std::string remote;
    if (!download(releaseUrl(L"version.txt"), versionFile) || !readFile(versionFile, remote) ||
        (remote = trim(remote)).empty()) {
        DeleteFileW(versionFile.c_str());
        // older releases have no version.txt: still grab the newest Enta.dll like the old launcher did
        if (download(releaseUrl(kDllName), pendingDllPath())) {
            if (installPendingDll(""))
                setUpdateText(L"Enta downloaded (" + channelName + L"). Version info will show after the next build.");
            else
                setUpdateText(L"Enta update downloaded. Close Minecraft fully to use it.", true);
        } else {
            setUpdateText(gDevChannel ? L"No test build published yet." : L"Couldn't check for updates (offline?).",
                          true);
        }
        gUpdating = false;
        return;
    }
    DeleteFileW(versionFile.c_str());

    // 1. the launcher itself
    if (!gJustUpdated && !myBuild.empty() && remote != myBuild) {
        setUpdateText(L"Updating the launcher...");
        if (selfUpdate()) return; // a fresh launcher is starting
        setUpdateText(L"Couldn't update the launcher. Is its folder read-only?", true);
    }

    // 2. Enta.dll
    if (installedDllVersion() != remote || !fileExists(dllPath())) {
        setUpdateText(L"Downloading the newest Enta...");
        if (download(releaseUrl(kDllName), pendingDllPath())) {
            if (installPendingDll(remote)) {
                setPendingVersion("");
            } else {
                setPendingVersion(remote); // Minecraft is using the old one
                setUpdateText(L"Enta update downloaded. Close Minecraft fully to use it.", true);
                gUpdating = false;
                return;
            }
        } else if (!fileExists(dllPath())) {
            setUpdateText(L"Couldn't download Enta.dll.", true);
            gUpdating = false;
            return;
        }
    }

    setUpdateText(L"Up to date (" + channelName + L") · build " + widen(remote));
    gUpdating = false;
}

void startUpdateCheck() {
    if (gUpdating.exchange(true)) return;
    std::thread(updateWorker).detach();
}

// ------------------------------------------------------------------------------------------------ launching

void launchWorker() {
    // wait for a running update check so we don't load a half-downloaded DLL
    for (int i = 0; i < 600 && gUpdating; i++) Sleep(100);

    std::wstring dll = dllPath();
    DWORD pid = findGame();
    if (!pid || !alreadyLoaded(pid)) installPendingDll(pendingVersion()); // game closed since the download

    if (!fileExists(dll)) {
        setStatus(L"Enta.dll is missing. Check your connection and try again.");
        gBusy = false;
        return;
    }
    grantAppContainerRead(dll);

    if (!pid) {
        setStatus(L"Starting Minecraft...");
        ShellExecuteW(nullptr, L"open", L"minecraft:", nullptr, nullptr, SW_SHOWNORMAL);
        for (int i = 0; i < 120 && !(pid = findGame()); i++) Sleep(500);
        if (!pid) {
            setStatus(L"Minecraft didn't start. Is Bedrock installed?");
            gBusy = false;
            return;
        }
        setStatus(L"Waiting for Minecraft to load...");
        Sleep(6000);
    }

    if (alreadyLoaded(pid)) {
        setStatus(L"Enta is already running. Press M in game.");
        gBusy = false;
        return;
    }
    setStatus(L"Loading Enta...");
    std::wstring err;
    if (loadInto(pid, dll, err))
        setStatus(L"Done! Press M in game to open Enta.");
    else
        setStatus(err);
    gBusy = false;
}

// ------------------------------------------------------------------------------------------------ skin

bool loadPng(std::wstring const& path, skin::Image& out) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&factory))))
        return false;

    bool ok = false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024) {
            out.w = static_cast<int>(w);
            out.h = static_cast<int>(h);
            out.px.resize(static_cast<size_t>(w) * h);
            // BGRA bytes in memory == 0xAARRGGBB as little-endian uint32
            ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(out.px.data())));
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    factory->Release();
    return ok;
}

std::wstring gameSkinPath() { return dataDir() + L"\\skin.png"; }
std::wstring pendingSkinFile() { return dataDir() + L"\\pending_skin.txt"; }
std::wstring appliedSkinFile() { return dataDir() + L"\\skin_applied.txt"; }

std::wstring skinsDir() {
    std::wstring d = dataDir() + L"\\skins";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// first line of a small UTF-8 text file
std::wstring readPathFile(std::wstring const& file) {
    std::string t;
    if (!readFile(file, t)) return {};
    auto nl = t.find('\n');
    return widen(trim(nl == std::string::npos ? t : t.substr(0, nl)));
}

// cached; refreshed with the skin list (every couple of seconds) and after changes here
std::wstring gPendingSkin, gAppliedSkin;
std::wstring pendingSkin() { return gPendingSkin; }
std::wstring appliedSkin() { return gAppliedSkin; }

SkinEntry const* selectedEntry() {
    for (auto& e : gSkins)
        if (_wcsicmp(e.path.c_str(), gSelectedSkin.c_str()) == 0) return &e;
    return gSkins.empty() ? nullptr : &gSkins.front();
}

void refreshSkinList() {
    gPendingSkin = readPathFile(pendingSkinFile());
    gAppliedSkin = readPathFile(appliedSkinFile());
    std::vector<SkinEntry> list;
    if (fileExists(gameSkinPath())) list.push_back({ gameSkinPath(), L"Your in-game skin", true });

    std::vector<SkinEntry> added;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((skinsDir() + L"\\*.png").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            added.push_back({ skinsDir() + L"\\" + name, name.substr(0, name.size() - 4), false });
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(added.begin(), added.end(), [](SkinEntry const& a, SkinEntry const& b) {
        return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0;
    });
    list.insert(list.end(), added.begin(), added.end());
    gSkins = std::move(list);
}

// decode the selected skin again if it changed (or the game exported a new one)
void reloadSkinIfChanged() {
    refreshSkinList();
    SkinEntry const* e = selectedEntry();
    if (!e) {
        gSkin = {};
        gLoadedSkinPath.clear();
        return;
    }
    FILETIME ft {};
    if (!fileTime(e->path, ft)) return;
    if (e->path == gLoadedSkinPath && sameTime(ft, gSkinTime) && gSkin.valid()) return;
    skin::Image img;
    if (loadPng(e->path, img)) {
        gSkin = img;
        gSkinTime = ft;
        gLoadedSkinPath = e->path;
    }
}

void selectSkin(std::wstring const& path) {
    gSelectedSkin = path;
    saveSettings();
    reloadSkinIfChanged();
}

void stepSkin(int dir) {
    if (gSkins.empty()) return;
    int idx = 0;
    if (auto* e = selectedEntry()) idx = static_cast<int>(e - gSkins.data());
    idx = (idx + dir + static_cast<int>(gSkins.size())) % static_cast<int>(gSkins.size());
    selectSkin(gSkins[idx].path);
}

void addSkin() {
    wchar_t file[MAX_PATH] {};
    OPENFILENAMEW ofn {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gWnd;
    ofn.lpstrFilter = L"Minecraft skin (*.png)\0*.png\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Add a skin";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    skin::Image img;
    if (!loadPng(file, img) || !skin::isSupported(img)) {
        setStatus(L"That isn't a normal Minecraft skin (64x64, 64x32 or 128x128).");
        return;
    }

    // copy it into the library under a free name
    std::wstring src = file;
    std::wstring base = src.substr(src.find_last_of(L"\\/") + 1);
    if (base.size() > 4 && _wcsicmp(base.c_str() + base.size() - 4, L".png") == 0) base.resize(base.size() - 4);
    std::wstring dest = skinsDir() + L"\\" + base + L".png";
    for (int n = 2; fileExists(dest) && n < 100; n++) dest = skinsDir() + L"\\" + base + L" (" + std::to_wstring(n) + L").png";
    if (!CopyFileW(src.c_str(), dest.c_str(), TRUE)) {
        setStatus(L"Couldn't add that skin.");
        return;
    }
    selectSkin(dest);
    setStatus(L"Skin added. Press \"Use in game\" to wear it.");
}

void removeSkin() {
    SkinEntry const* e = selectedEntry();
    if (!e || e->fromGame) return;
    std::wstring path = e->path;
    if (_wcsicmp(pendingSkin().c_str(), path.c_str()) == 0) DeleteFileW(pendingSkinFile().c_str());
    DeleteFileW(path.c_str());
    gSelectedSkin.clear();
    saveSettings();
    reloadSkinIfChanged();
}

// Tell Enta to answer the game's "Choose New Skin" picker with this file.
void useSkinInGame() {
    SkinEntry const* e = selectedEntry();
    if (!e || e->fromGame) return;
    grantAppContainerRead(skinsDir());
    grantAppContainerRead(e->path);
    DeleteFileW(appliedSkinFile().c_str());
    if (!writeFile(pendingSkinFile(), narrow(e->path) + "\n")) {
        setStatus(L"Couldn't save your choice.");
        return;
    }
    gPendingSkin = e->path;
    gAppliedSkin.clear();
    setStatus(L"In game: Dressing Room > Classic Skins > Choose New Skin.");
}

// ------------------------------------------------------------------------------------------------ mods

std::wstring modsPath() { return dataDir() + L"\\modules.tsv"; }
std::wstring overridesPath() { return dataDir() + L"\\module_overrides.tsv"; }

std::wstring categoryName(int c) {
    switch (c) {
    case 1: return L"HUD";
    case 2: return L"HUD";
    case 3: return L"Script";
    default: return L"Game";
    }
}

void reloadModsIfChanged() {
    FILETIME ft {};
    if (!fileTime(modsPath(), ft)) return;
    if (sameTime(ft, gModsTime) && !gMods.empty()) return;

    std::string text;
    if (!readFile(modsPath(), text)) return;
    gModsTime = ft;

    // choices you made here that the game hasn't picked up yet
    std::vector<std::pair<std::string, bool>> pending;
    std::string ov;
    if (readFile(overridesPath(), ov)) {
        std::istringstream os(ov);
        for (std::string line; std::getline(os, line);) {
            line = trim(line);
            auto tab = line.find('\t');
            if (line.empty() || line[0] == '#' || tab == std::string::npos) continue;
            pending.emplace_back(line.substr(0, tab), line.compare(tab + 1, 1, "1") == 0);
        }
    }

    std::vector<Mod> mods;
    std::istringstream ss(text);
    for (std::string line; std::getline(ss, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::istringstream ls(line);
        for (std::string c; std::getline(ls, c, '\t');) cols.push_back(c);
        if (cols.size() < 4) continue;

        Mod m;
        m.name = cols[0];
        m.display = widen(cols[1]);
        m.category = std::atoi(cols[2].c_str());
        m.savedOn = m.on = cols[3] == "1";
        for (auto& [n, on] : pending)
            if (n == m.name) m.on = on;
        mods.push_back(std::move(m));
    }
    std::sort(mods.begin(), mods.end(), [](Mod const& a, Mod const& b) {
        return _wcsicmp(a.display.c_str(), b.display.c_str()) < 0;
    });
    gMods = std::move(mods);
}

void saveOverrides() {
    std::string out = "# set in EntaLauncher, applied when Enta starts\n";
    bool any = false;
    for (auto& m : gMods) {
        if (m.on == m.savedOn) continue;
        out += m.name + "\t" + (m.on ? "1" : "0") + "\n";
        any = true;
    }
    if (any)
        writeFile(overridesPath(), out);
    else
        DeleteFileW(overridesPath().c_str());
}

int maxScroll() {
    int content = static_cast<int>(gMods.size()) * kRowH;
    int view = kList.bottom - kList.top;
    return std::max(0, content - view);
}

int rowAt(POINT p) {
    if (!PtInRect(&kList, p)) return -1;
    int idx = (p.y - kList.top + gScroll) / kRowH;
    return idx >= 0 && idx < static_cast<int>(gMods.size()) ? idx : -1;
}

// ------------------------------------------------------------------------------------------------ painting

HFONT font(int size, int weight) {
    return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void fillRound(HDC dc, RECT r, int radius, COLORREF fill, COLORREF line) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void text(HDC dc, std::wstring const& s, RECT r, HFONT f, COLORREF c, UINT flags) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), -1, &r, flags | DT_NOPREFIX);
}

void line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int width) {
    HPEN p = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, p);
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
    SelectObject(dc, op);
    DeleteObject(p);
}

void toggle(HDC dc, int x, int y, bool on, bool hot) {
    RECT track = { x, y, x + 38, y + 20 };
    fillRound(dc, track, 20, on ? (hot ? kPurpleHot : kPurple) : (hot ? kPurpleDim : kOff), on ? kPurple : kPurpleDim);
    int knobX = on ? x + 20 : x + 2;
    RECT knob = { knobX, y + 2, knobX + 16, y + 18 };
    HBRUSH b = CreateSolidBrush(kText);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, knob.left, knob.top, knob.right, knob.bottom);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
}

void paintViewer(HDC dc) {
    const int w = kViewer.right - kViewer.left, h = kViewer.bottom - kViewer.top;
    if (!skin::isSupported(gSkin)) {
        RECT r = kViewer;
        r.top += h / 2 - 30;
        text(dc, L"Play once with Enta to see\nyour skin here, or add one\nwith \"Add skin\".", r, gBodyFont,
             kSubText, DT_CENTER | DT_WORDBREAK);
        return;
    }

    if (!gViewerBmp) {
        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        gViewerBmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &gViewerBits, nullptr, 0);
        if (!gViewerBmp) return;
    }

    std::vector<uint32_t> px;
    skin::renderSmooth(gSkin, gYaw, 12.f, w, h, px); // premultiplied ARGB, what AlphaBlend wants
    std::copy(px.begin(), px.end(), static_cast<uint32_t*>(gViewerBits));

    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, gViewerBmp);
    BLENDFUNCTION bf { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, kViewer.left, kViewer.top, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

void arrow(HDC dc, RECT r, bool left, COLORREF c) {
    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    POINT pts[3] = { { left ? cx + 4 : cx - 4, cy - 7 }, { left ? cx + 4 : cx - 4, cy + 7 }, { left ? cx - 5 : cx + 5, cy } };
    HBRUSH b = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, pts, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
}

void paintSkinControls(HDC dc) {
    SkinEntry const* e = selectedEntry();
    const bool many = gSkins.size() > 1;
    if (many) {
        arrow(dc, kPrev, true, gHoverSkin == SB_Prev ? kPurpleHot : kSubText);
        arrow(dc, kNext, false, gHoverSkin == SB_Next ? kPurpleHot : kSubText);
    }
    if (e) {
        text(dc, e->label, kSkinName, gBodyFont, kText, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (!e->fromGame)
            text(dc, L"Remove", kRemoveSkin, gSmallFont, gHoverSkin == SB_Remove ? kPurpleHot : kSubText,
                 DT_RIGHT | DT_SINGLELINE);
    }

    // where this skin stands
    std::wstring note;
    COLORREF noteColor = kSubText;
    if (e && !e->fromGame) {
        if (_wcsicmp(pendingSkin().c_str(), e->path.c_str()) == 0) {
            note = L"Ready: press Choose New Skin in game";
            noteColor = kWarn;
        } else if (_wcsicmp(appliedSkin().c_str(), e->path.c_str()) == 0) {
            note = L"Sent to the game";
        } else if (skin::isSupported(gSkin)) {
            note = skin::isSlim(gSkin) ? L"Slim arms" : L"Classic arms";
        }
    } else if (e) {
        note = L"Updates every time you play";
    }
    text(dc, note, kSkinNote, gSmallFont, noteColor, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // buttons
    fillRound(dc, kAddSkin, 12, gHoverSkin == SB_Add ? kPanelHover : kPanel, kPurpleDim);
    text(dc, L"Add skin", kAddSkin, gBodyFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const bool canUse = e && !e->fromGame;
    fillRound(dc, kUseSkin, 12, !canUse ? kPurpleDim : (gHoverSkin == SB_Use ? kPurpleHot : kPurple),
              canUse ? kPurple : kPurpleDim);
    text(dc, canUse ? L"Use in game" : L"Wearing this", kUseSkin, gBodyFont, canUse ? kText : kSubText,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd, &ps);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, kW, kH);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    std::wstring status, updateText;
    bool updateWarn;
    {
        std::lock_guard g(gLock);
        status = gStatus;
        updateText = gUpdateText;
        updateWarn = gUpdateWarn;
    }

    RECT all = { 0, 0, kW, kH };
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &all, bg);
    DeleteObject(bg);

    // window border
    HPEN border = CreatePen(PS_SOLID, 2, kPurple);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 1, 1, kW - 1, kH - 1, 18, 18);

    // logo ring + E
    HPEN ring = CreatePen(PS_SOLID, 4, kPurple);
    SelectObject(dc, ring);
    Ellipse(dc, 26, 22, 78, 74);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(ring);
    DeleteObject(border);
    text(dc, L"E", { 26, 22, 78, 74 }, gTitleFont, kPurpleHot, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, L"ENTA", { 92, 22, 400, 54 }, gTitleFont, kText, DT_LEFT | DT_SINGLELINE);
    text(dc, L"Minecraft Bedrock client", { 94, 54, 400, 78 }, gBodyFont, kSubText, DT_LEFT | DT_SINGLELINE);

    // minimize + close, drawn as lines so they never show up as garbled text
    line(dc, kMin.left + 8, 28, kMin.right - 8, 28, gHoverMin ? kText : kSubText, 2);
    line(dc, kClose.left + 8, kClose.top + 8, kClose.right - 8, kClose.bottom - 8, gHoverClose ? kText : kSubText, 2);
    line(dc, kClose.right - 8, kClose.top + 8, kClose.left + 8, kClose.bottom - 8, gHoverClose ? kText : kSubText, 2);

    // ---- skin panel
    fillRound(dc, kSkinPanel, 16, kPanel, kPurpleDim);
    text(dc, L"SKINS", { kSkinPanel.left + 14, kSkinPanel.top + 8, kSkinPanel.right, kSkinPanel.top + 28 },
         gHeadFont, kSubText, DT_LEFT | DT_SINGLELINE);
    paintViewer(dc);
    paintSkinControls(dc);

    // ---- mods panel
    fillRound(dc, kModsPanel, 16, kPanel, kPurpleDim);
    int onCount = 0;
    for (auto& m : gMods) onCount += m.on ? 1 : 0;
    text(dc, L"MODS", { kModsPanel.left + 14, kModsPanel.top + 8, kModsPanel.right, kModsPanel.top + 28 }, gHeadFont,
         kSubText, DT_LEFT | DT_SINGLELINE);
    if (!gMods.empty())
        text(dc, std::to_wstring(onCount) + L" of " + std::to_wstring(gMods.size()) + L" on",
             { kModsPanel.left, kModsPanel.top + 8, kModsPanel.right - 16, kModsPanel.top + 28 }, gSmallFont, kSubText,
             DT_RIGHT | DT_SINGLELINE);

    bool changed = false;
    for (auto& m : gMods) changed |= m.on != m.savedOn;
    std::wstring hint = gMods.empty() ? L""
                        : gGameHasEnta ? L"Minecraft is running: changes apply next time it starts."
                        : changed      ? L"Changes apply when you press Launch."
                                       : L"Switch mods off here before you start the game.";
    text(dc, hint, { kModsPanel.left + 14, kModsPanel.top + 32, kModsPanel.right - 14, kModsPanel.top + 54 },
         gSmallFont, gGameHasEnta && changed ? kWarn : kSubText, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (gMods.empty()) {
        RECT r = kList;
        r.top += 60;
        text(dc, L"Launch once with this launcher\nto load your mods list.", r, gBodyFont, kSubText,
             DT_CENTER | DT_WORDBREAK);
    } else {
        HRGN clip = CreateRectRgn(kList.left, kList.top, kList.right, kList.bottom);
        SelectClipRgn(dc, clip);
        for (int i = 0; i < static_cast<int>(gMods.size()); i++) {
            int y = kList.top + i * kRowH - gScroll;
            if (y + kRowH < kList.top || y > kList.bottom) continue;
            auto& m = gMods[i];
            RECT row = { kList.left, y + 2, kList.right - 8, y + kRowH - 2 };
            if (i == gHoverRow) fillRound(dc, row, 10, kPanelHover, kPanelHover);
            text(dc, m.display, { row.left + 10, row.top, row.right - 120, row.bottom }, gBodyFont,
                 m.on ? kText : kSubText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            text(dc, categoryName(m.category), { row.right - 120, row.top, row.right - 56, row.bottom }, gSmallFont,
                 kSubText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            toggle(dc, row.right - 46, row.top + (row.bottom - row.top - 20) / 2, m.on, i == gHoverRow);
        }
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);

        // scrollbar
        int ms = maxScroll();
        if (ms > 0) {
            int viewH = kList.bottom - kList.top;
            int thumbH = std::max(24, viewH * viewH / (viewH + ms));
            int thumbY = kList.top + (viewH - thumbH) * gScroll / ms;
            fillRound(dc, { kList.right - 5, thumbY, kList.right - 1, thumbY + thumbH }, 4, kPurpleDim, kPurpleDim);
        }
    }

    // ---- bottom bar
    toggle(dc, kChannel.left, kChannel.top + 2, gDevChannel, gHoverChannel);
    text(dc, L"Test builds", { kChannel.left + 48, kChannel.top, kChannel.right, kChannel.bottom }, gBodyFont,
         gDevChannel ? kText : kSubText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    text(dc, updateText, kVersion, gSmallFont, updateWarn ? kWarn : kSubText, DT_LEFT | DT_WORDBREAK);

    const bool busy = gBusy;
    fillRound(dc, kLaunch, 14, busy ? kPurpleDim : (gHoverLaunch ? kPurpleHot : kPurple), busy ? kPurpleDim : kPurple);
    text(dc, busy ? L"WORKING..." : L"LAUNCH", kLaunch, gButtonFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, status, kStatus, gSmallFont, kSubText, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(wdc, 0, 0, kW, kH, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------------------------------------------ window

void onLaunch() {
    if (gBusy.exchange(true)) return;
    saveOverrides();
    InvalidateRect(gWnd, nullptr, FALSE);
    std::thread(launchWorker).detach();
}

// light checks every couple of seconds: new skin/mods from the game, and whether Enta is running
void periodicCheck() {
    reloadSkinIfChanged();
    reloadModsIfChanged();
    std::thread([] {
        DWORD pid = findGame();
        bool running = pid && alreadyLoaded(pid);
        if (running != gGameHasEnta.exchange(running)) refresh();
    }).detach();
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    static int tick = 0;
    switch (msg) {
    case WM_PAINT: paint(h); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_TIMER: {
        if (IsIconic(h)) return 0;
        // spin slowly, but pause a moment after you drag it
        if (!gDragging && GetTickCount() - gLastInput > 1500) gYaw = std::fmod(gYaw + 1.2f, 360.f);
        InvalidateRect(h, &kViewer, FALSE);
        if (++tick % 60 == 0) {
            periodicCheck();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (gDragging) {
            gYaw += static_cast<float>(p.x - gDragX) * 0.8f;
            gDragX = p.x;
            gLastInput = GetTickCount();
            InvalidateRect(h, &kViewer, FALSE);
            return 0;
        }
        bool l = PtInRect(&kLaunch, p) != 0, c = PtInRect(&kClose, p) != 0, m = PtInRect(&kMin, p) != 0,
             ch = PtInRect(&kChannel, p) != 0;
        int sb = PtInRect(&kPrev, p)       ? SB_Prev
                 : PtInRect(&kNext, p)     ? SB_Next
                 : PtInRect(&kAddSkin, p)  ? SB_Add
                 : PtInRect(&kUseSkin, p)  ? SB_Use
                 : PtInRect(&kRemoveSkin, p) ? SB_Remove
                                             : SB_None;
        int row = rowAt(p);
        if (l != gHoverLaunch || c != gHoverClose || m != gHoverMin || sb != gHoverSkin || ch != gHoverChannel ||
            row != gHoverRow) {
            gHoverLaunch = l;
            gHoverClose = c;
            gHoverMin = m;
            gHoverSkin = sb;
            gHoverChannel = ch;
            gHoverRow = row;
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        gHoverLaunch = gHoverClose = gHoverMin = gHoverChannel = false;
        gHoverSkin = SB_None;
        gHoverRow = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;

    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &p);
        if (PtInRect(&kModsPanel, p)) {
            gScroll = std::clamp(gScroll - GET_WHEEL_DELTA_WPARAM(wp) / 120 * kRowH * 2, 0, maxScroll());
            gHoverRow = rowAt(p);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (PtInRect(&kClose, p)) {
            DestroyWindow(h);
            return 0;
        }
        if (PtInRect(&kMin, p)) {
            ShowWindow(h, SW_MINIMIZE);
            return 0;
        }
        if (PtInRect(&kLaunch, p)) {
            onLaunch();
            return 0;
        }
        if (PtInRect(&kPrev, p) || PtInRect(&kNext, p)) {
            stepSkin(PtInRect(&kPrev, p) ? -1 : 1);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&kAddSkin, p)) {
            addSkin();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&kUseSkin, p)) {
            useSkinInGame();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&kRemoveSkin, p)) {
            removeSkin();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&kChannel, p)) {
            gDevChannel = !gDevChannel;
            saveSettings();
            startUpdateCheck();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&kViewer, p)) {
            gDragging = true;
            gDragX = p.x;
            gLastInput = GetTickCount();
            SetCapture(h);
            return 0;
        }
        int row = rowAt(p);
        if (row >= 0) {
            gMods[row].on = !gMods[row].on;
            saveOverrides();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&kModsPanel, p) || PtInRect(&kSkinPanel, p)) return 0;
        // drag the borderless window from anywhere else
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_LBUTTONUP:
        if (gDragging) {
            gDragging = false;
            gLastInput = GetTickCount();
            ReleaseCapture();
        }
        return 0;

    case WM_APP_REFRESH: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_APP_QUIT: DestroyWindow(h); return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_ANIM);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    gJustUpdated = cmdLine && wcsstr(cmdLine, L"--updated") != nullptr;
    // leftover from a self-update; may still be locked for a moment
    std::thread([] {
        std::wstring old = exeDir() + L"\\EntaLauncher.old.exe";
        for (int i = 0; i < 20 && fileExists(old) && !DeleteFileW(old.c_str()); i++) Sleep(500);
    }).detach();

    loadSettings();
    reloadSkinIfChanged();
    reloadModsIfChanged();

    gTitleFont = font(34, FW_BOLD);
    gBodyFont = font(17, FW_NORMAL);
    gSmallFont = font(15, FW_NORMAL);
    gHeadFont = font(14, FW_BOLD);
    gButtonFont = font(22, FW_SEMIBOLD);

    WNDCLASSW wc {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"EntaLauncher";
    RegisterClassW(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - kW) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - kH) / 2;
    gWnd = CreateWindowExW(0, wc.lpszClassName, L"Enta Launcher", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, kW, kH,
                           nullptr, nullptr, inst, nullptr);
    HRGN rgn = CreateRoundRectRgn(0, 0, kW + 1, kH + 1, 18, 18);
    SetWindowRgn(gWnd, rgn, TRUE);
    ShowWindow(gWnd, show);
    SetTimer(gWnd, TIMER_ANIM, 33, nullptr);
    periodicCheck();
    startUpdateCheck();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return 0;
}
