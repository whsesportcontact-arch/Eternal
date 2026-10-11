// Eternal Launcher
// Starts Minecraft Bedrock (Windows) and loads Eternal.dll into it.
// On launch it tries to grab the newest Eternal.dll from this repo's GitHub releases,
// and falls back to an Eternal.dll sitting next to the launcher.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <urlmon.h>
#include <wininet.h>
#include <string>
#include <thread>
#include <atomic>

#ifndef ETERNAL_DLL_URL
#define ETERNAL_DLL_URL L""
#endif

namespace {

constexpr wchar_t kGameExe[] = L"Minecraft.Windows.exe";
constexpr wchar_t kDllName[] = L"Eternal.dll";

// theme
constexpr COLORREF kBg = RGB(0x08, 0x05, 0x0E);
constexpr COLORREF kPanel = RGB(0x14, 0x0C, 0x20);
constexpr COLORREF kPurple = RGB(0x8B, 0x3D, 0xFF);
constexpr COLORREF kPurpleHot = RGB(0xA6, 0x6B, 0xFF);
constexpr COLORREF kPurpleDim = RGB(0x3B, 0x2E, 0x4D);
constexpr COLORREF kText = RGB(0xF2, 0xEE, 0xFF);
constexpr COLORREF kSubText = RGB(0x9C, 0x8F, 0xB5);

constexpr int kW = 420, kH = 300;
RECT gButton = { 60, 170, 360, 222 };
RECT gClose = { kW - 40, 10, kW - 12, 38 };

HWND gWnd = nullptr;
HFONT gTitleFont, gBodyFont, gButtonFont;
std::atomic_bool gBusy = false;
bool gHover = false, gCloseHover = false;
std::wstring gStatus = L"Ready.";

#define WM_APP_STATUS (WM_APP + 1)

void setStatus(std::wstring const& s) {
    auto* copy = new std::wstring(s);
    PostMessageW(gWnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(copy));
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    return p.substr(0, p.find_last_of(L"\\/"));
}

bool fileExists(std::wstring const& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

DWORD findGame() {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, kGameExe) == 0) { pid = pe.th32ProcessID; break; }
    }
    CloseHandle(snap);
    return pid;
}

bool alreadyLoaded(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{ sizeof(me) };
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        if (_wcsicmp(me.szModule, kDllName) == 0) { found = true; break; }
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
        EXPLICIT_ACCESSW ea{};
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
    if (!proc) { err = L"Couldn't open Minecraft (error " + std::to_wstring(GetLastError()) + L")."; return false; }

    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    if (remote && WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
        if (th) {
            WaitForSingleObject(th, 15000);
            DWORD code = 0;
            GetExitCodeThread(th, &code);
            ok = code != 0; // LoadLibrary returned a module handle
            if (!ok) err = L"Minecraft refused to load Eternal.dll (wrong game version?).";
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

void launchWorker() {
    std::wstring dir = exeDir();
    std::wstring dll = dir + L"\\" + kDllName;

    // 1. get the newest DLL
    std::wstring url = ETERNAL_DLL_URL;
    if (!url.empty()) {
        setStatus(L"Checking for updates...");
        std::wstring tmp = dll + L".download";
        DeleteUrlCacheEntryW(url.c_str());
        if (SUCCEEDED(URLDownloadToFileW(nullptr, url.c_str(), tmp.c_str(), 0, nullptr))) {
            // can't replace the file while Minecraft has it loaded; that's fine, we'll keep the old one
            if (!MoveFileExW(tmp.c_str(), dll.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
        } else if (!fileExists(dll)) {
            setStatus(L"Download failed and no Eternal.dll next to the launcher.");
            gBusy = false;
            return;
        }
    }
    if (!fileExists(dll)) {
        setStatus(L"Eternal.dll is missing. Put it next to the launcher.");
        gBusy = false;
        return;
    }
    grantAppContainerRead(dll);

    // 2. start the game if it isn't running
    DWORD pid = findGame();
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

    // 3. load Eternal
    if (alreadyLoaded(pid)) {
        setStatus(L"Eternal is already running. Press M in game.");
        gBusy = false;
        return;
    }
    setStatus(L"Loading Eternal...");
    std::wstring err;
    if (loadInto(pid, dll, err))
        setStatus(L"Done! Press M in game to open Eternal.");
    else
        setStatus(err);
    gBusy = false;
}

void paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    // double buffer
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, kW, kH);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    RECT all = { 0, 0, kW, kH };
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &all, bg);
    DeleteObject(bg);

    // purple border
    HPEN border = CreatePen(PS_SOLID, 2, kPurple);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 1, 1, kW - 1, kH - 1, 18, 18);

    // logo ring + E
    HPEN ring = CreatePen(PS_SOLID, 4, kPurple);
    SelectObject(dc, ring);
    Ellipse(dc, 30, 34, 82, 86);
    SelectObject(dc, gTitleFont);
    SetTextColor(dc, kPurpleHot);
    RECT eR = { 30, 34, 82, 86 };
    DrawTextW(dc, L"E", -1, &eR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // title
    SetTextColor(dc, kText);
    RECT t = { 96, 34, kW - 50, 66 };
    DrawTextW(dc, L"ETERNAL", -1, &t, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, gBodyFont);
    SetTextColor(dc, kSubText);
    RECT st = { 98, 66, kW - 20, 90 };
    DrawTextW(dc, L"Minecraft Bedrock client", -1, &st, DT_LEFT | DT_SINGLELINE);

    // close X
    SetTextColor(dc, gCloseHover ? kText : kSubText);
    DrawTextW(dc, L"✕", -1, &gClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // launch button
    HBRUSH btn = CreateSolidBrush(gBusy ? kPurpleDim : (gHover ? kPurpleHot : kPurple));
    HPEN btnPen = CreatePen(PS_SOLID, 1, gBusy ? kPurpleDim : kPurple);
    SelectObject(dc, btn);
    SelectObject(dc, btnPen);
    RoundRect(dc, gButton.left, gButton.top, gButton.right, gButton.bottom, 14, 14);
    SelectObject(dc, gButtonFont);
    SetTextColor(dc, kText);
    DrawTextW(dc, gBusy ? L"WORKING..." : L"LAUNCH", -1, &gButton, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // status panel
    SelectObject(dc, gBodyFont);
    SetTextColor(dc, kSubText);
    RECT s = { 20, 236, kW - 20, 280 };
    DrawTextW(dc, gStatus.c_str(), -1, &s, DT_CENTER | DT_WORDBREAK);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    DeleteObject(ring);
    DeleteObject(btn);
    DeleteObject(btnPen);

    BitBlt(wdc, 0, 0, kW, kH, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        bool hov = PtInRect(&gButton, p), ch = PtInRect(&gClose, p);
        if (hov != gHover || ch != gCloseHover) { gHover = hov; gCloseHover = ch; InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: gHover = gCloseHover = false; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (PtInRect(&gClose, p)) { DestroyWindow(h); return 0; }
        if (PtInRect(&gButton, p)) {
            if (!gBusy.exchange(true)) {
                InvalidateRect(h, nullptr, FALSE);
                std::thread(launchWorker).detach();
            }
            return 0;
        }
        // drag the borderless window from anywhere else
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_APP_STATUS: {
        auto* s = reinterpret_cast<std::wstring*>(lp);
        gStatus = *s;
        delete s;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HFONT font(int size, int weight) {
    return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    gTitleFont = font(34, FW_BOLD);
    gBodyFont = font(17, FW_NORMAL);
    gButtonFont = font(22, FW_SEMIBOLD);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"EternalLauncher";
    RegisterClassW(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - kW) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - kH) / 2;
    gWnd = CreateWindowExW(0, wc.lpszClassName, L"Eternal Launcher", WS_POPUP | WS_MINIMIZEBOX, x, y, kW, kH,
                           nullptr, nullptr, inst, nullptr);
    HRGN rgn = CreateRoundRectRgn(0, 0, kW + 1, kH + 1, 18, 18);
    SetWindowRgn(gWnd, rgn, TRUE);
    ShowWindow(gWnd, show);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
