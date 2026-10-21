#include "platform.h"

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE
  #define _DARWIN_C_SOURCE
#endif
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <shellapi.h>
  #include <shlobj.h>

bool ShsPickFolder(const char *title, char *out, size_t cap)
{
    wchar_t wtitle[128] = L"Select folder";
    if (title) MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 128);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = wtitle;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;

    wchar_t wpath[MAX_PATH];
    bool ok = SHGetPathFromIDListW(pidl, wpath) != 0;
    CoTaskMemFree(pidl);
    if (ok) ok = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, (int)cap, NULL, NULL) > 0;
    return ok;
}

double ShsFreeDiskGiB(const char *path)
{
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return -1.0;
    ULARGE_INTEGER freeBytes;
    if (!GetDiskFreeSpaceExW(w, &freeBytes, NULL, NULL)) return -1.0;
    return (double)freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
}

/* ---- system tray ---------------------------------------------------------- */

#define WM_SHS_TRAY (WM_APP + 1)
#define WM_SHS_TIP  (WM_APP + 2)
#define ID_TRAY_OPEN 1001
#define ID_TRAY_SYNC 1002
#define ID_TRAY_QUIT 1003

static HWND             g_trayWnd;
static HANDLE           g_trayThread;
static HANDLE           g_trayReady;
static volatile LONG    g_trayEvents;
static volatile LONG    g_trayOk;
static NOTIFYICONDATAW  g_nid;
static UINT             g_taskbarCreated;

static HICON TrayIcon(void)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    HICON ic = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    return ic ? ic : LoadIconW(NULL, MAKEINTRESOURCEW(32512))   /* IDI_APPLICATION */;
}

static LRESULT CALLBACK TrayProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_SHS_TRAY) {
        UINT ev = LOWORD(l);
        if (ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK) {
            InterlockedOr(&g_trayEvents, SHS_TRAY_SHOW);
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open SailHighSea SimpleSync");
            AppendMenuW(menu, MF_STRING, ID_TRAY_SYNC, L"Sync all");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_TRAY_QUIT, L"Quit");
            SetForegroundWindow(h);                              /* required so the menu closes when clicking elsewhere */
            int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd == ID_TRAY_OPEN) InterlockedOr(&g_trayEvents, SHS_TRAY_SHOW);
            if (cmd == ID_TRAY_SYNC) InterlockedOr(&g_trayEvents, SHS_TRAY_SYNC_ALL);
            if (cmd == ID_TRAY_QUIT) InterlockedOr(&g_trayEvents, SHS_TRAY_QUIT);
        }
        return 0;
    }
    if (m == WM_SHS_TIP) {
        g_nid.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        return 0;
    }
    if (m == g_taskbarCreated && m != 0) {                       /* Explorer restarted: put the icon back */
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    if (m == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static DWORD WINAPI TrayThread(LPVOID unused)
{
    (void)unused;
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = TrayProc; wc.hInstance = inst; wc.lpszClassName = L"ShsTrayWindow";
    RegisterClassW(&wc);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_trayWnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"SailHighSea SimpleSync tray", WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (g_trayWnd) {
        g_nid.cbSize = sizeof g_nid;
        g_nid.hWnd = g_trayWnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_SHS_TRAY;
        g_nid.hIcon = TrayIcon();
        if (!g_nid.szTip[0]) wcscpy(g_nid.szTip, L"SailHighSea SimpleSync");
        InterlockedExchange(&g_trayOk, Shell_NotifyIconW(NIM_ADD, &g_nid) ? 1 : 0);
    }
    SetEvent(g_trayReady);
    if (g_trayWnd) {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}

bool ShsTraySupported(void) { return true; }
bool ShsTrayActive(void)    { return g_trayThread != NULL && g_trayOk; }

bool ShsTrayCreate(const char *tooltip)
{
    if (g_trayThread) return g_trayOk != 0;
    memset(&g_nid, 0, sizeof g_nid);
    if (tooltip) MultiByteToWideChar(CP_UTF8, 0, tooltip, -1, g_nid.szTip, 127);
    g_trayEvents = 0; g_trayOk = 0;
    g_trayReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_trayThread = CreateThread(NULL, 0, TrayThread, NULL, 0, NULL);
    if (!g_trayThread) { CloseHandle(g_trayReady); return false; }
    WaitForSingleObject(g_trayReady, 3000);
    CloseHandle(g_trayReady); g_trayReady = NULL;
    if (!g_trayOk) { ShsTrayDestroy(); return false; }
    return true;
}

void ShsTraySetTooltip(const char *tooltip)
{
    if (!g_trayThread || !g_trayWnd || !tooltip) return;
    MultiByteToWideChar(CP_UTF8, 0, tooltip, -1, g_nid.szTip, 127);
    PostMessageW(g_trayWnd, WM_SHS_TIP, 0, 0);
}

void ShsTrayDestroy(void)
{
    if (!g_trayThread) return;
    if (g_trayWnd) PostMessageW(g_trayWnd, WM_CLOSE, 0, 0);     /* DefWindowProc turns this into WM_DESTROY */
    WaitForSingleObject(g_trayThread, 2000);
    CloseHandle(g_trayThread);
    g_trayThread = NULL; g_trayWnd = NULL; g_trayOk = 0;
}

int ShsTrayPoll(void) { return (int)InterlockedExchange(&g_trayEvents, 0); }

/* ---- single instance: a named mutex marks the running copy; later copies poke it to show its window ---- */

static DWORD WINAPI ShowWaiter(LPVOID ev)
{
    for (;;) {
        if (WaitForSingleObject((HANDLE)ev, INFINITE) != WAIT_OBJECT_0) return 0;
        InterlockedOr(&g_trayEvents, SHS_TRAY_SHOW);
    }
}

void ShsTrimMemory(void) { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }
void ShsSleepMs(unsigned ms) { Sleep(ms); }

bool ShsRelaunch(int argc, char **argv)
{
    (void)argc; (void)argv;
    const wchar_t *cur = GetCommandLineW();
    size_t n = wcslen(cur) + 16;
    wchar_t *cmd = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (!cmd) return false;
    swprintf(cmd, n, L"%ls --restart", cur);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return ok != 0;
}

bool ShsSingleInstance(void)
{
    HANDLE m = CreateMutexW(NULL, FALSE, L"Local\\SailHighSea.SimpleSync.Instance");
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, L"Local\\SailHighSea.SimpleSync.Show");
    if (!m || !ev) return true;                            /* cannot tell: just run */
    if (GetLastError() == ERROR_ALREADY_EXISTS) {          /* the mutex call is the last one that set it */
        SetEvent(ev);
        CloseHandle(ev);
        CloseHandle(m);       /* a copy waiting to take over after a restart retries: its own handle must not keep the mutex alive */
        return false;
    }
    HANDLE t = CreateThread(NULL, 0, ShowWaiter, ev, 0, NULL);
    if (t) CloseHandle(t);
    return true;
}

/* ---- start with Windows (HKCU Run key) --------------------------------- */

#define RUN_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"SailHighSeaSimpleSync"

bool ShsAutostartEnabled(void)
{
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}

bool ShsAutostartRepair(bool startHidden)
{
    wchar_t cmd[MAX_PATH + 64];
    DWORD size = sizeof cmd;
    if (RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, NULL, cmd, &size) != ERROR_SUCCESS) return false;

    wchar_t exe[MAX_PATH + 1];                                       /* the program path: quoted, or up to the first space */
    const wchar_t *s = cmd, *e;
    if (*s == L'"') { s++; e = wcschr(s, L'"'); } else { e = wcschr(s, L' '); }
    size_t len = e ? (size_t)(e - s) : wcslen(s);
    if (len == 0 || len > MAX_PATH) return false;
    memcpy(exe, s, len * sizeof(wchar_t)); exe[len] = 0;
    if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) return false;   /* still there: leave it alone */
    return ShsAutostartSet(true, startHidden);
}

bool ShsAutostartSet(bool enable, bool startHidden)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS rc;
    if (enable) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\"%ls", exe, startHidden ? L" --minimized" : L"");
        rc = RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, RUN_VALUE);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}
/* ---- folder watcher (ReadDirectoryChangesW, recursive) ---------------------- */

struct ShsWatch {
    HANDLE         dir, thread, evt;
    volatile LONG  changed, stop;
    OVERLAPPED     ov;
    BYTE           buf[64 * 1024];
};

static DWORD WINAPI WatchThread(LPVOID p)
{
    ShsWatch *w = (ShsWatch *)p;
    while (!w->stop) {
        memset(&w->ov, 0, sizeof w->ov);
        w->ov.hEvent = w->evt;
        ResetEvent(w->evt);
        if (!ReadDirectoryChangesW(w->dir, w->buf, sizeof w->buf, TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION, NULL, &w->ov, NULL)) break;
        while (!w->stop && WaitForSingleObject(w->evt, 400) == WAIT_TIMEOUT) { }
        if (w->stop) { CancelIoEx(w->dir, &w->ov); break; }
        DWORD got = 0;
        GetOverlappedResult(w->dir, &w->ov, &got, FALSE);        /* got == 0 means the buffer overflowed: still a change */
        InterlockedExchange(&w->changed, 1);
    }
    return 0;
}

ShsWatch *ShsWatchStart(const char *folder)
{
    wchar_t wp[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, folder, -1, wp, 1024)) return NULL;
    HANDLE dir = CreateFileW(wp, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (dir == INVALID_HANDLE_VALUE) return NULL;
    ShsWatch *w = (ShsWatch *)calloc(1, sizeof *w);
    if (!w) { CloseHandle(dir); return NULL; }
    w->dir = dir;
    w->evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->thread = CreateThread(NULL, 0, WatchThread, w, 0, NULL);
    if (!w->evt || !w->thread) { ShsWatchStop(w); return NULL; }
    return w;
}

bool ShsWatchTake(ShsWatch *w) { return w && InterlockedExchange(&w->changed, 0) != 0; }

void ShsWatchStop(ShsWatch *w)
{
    if (!w) return;
    InterlockedExchange(&w->stop, 1);
    if (w->thread) { WaitForSingleObject(w->thread, 2000); CloseHandle(w->thread); }
    if (w->evt) CloseHandle(w->evt);
    if (w->dir) CloseHandle(w->dir);
    free(w);
}
bool ShsOpenFolder(const char *path)
{
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return false;
    return (INT_PTR)ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL) > 32;
}

bool ShsOpenUrl(const char *url) { return ShsOpenFolder(url); }   /* ShellExecute "open" handles both */

void ShsRoundWindowCorners(void *nativeHandle)
{
    /* DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2. Looked up at run time so older Windows still starts. */
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT (WINAPI *SetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
    SetAttrFn setAttr = (SetAttrFn)(void (*)(void))GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (setAttr && nativeHandle) { DWORD pref = 2; setAttr((HWND)nativeHandle, 33, &pref, sizeof pref); }
    FreeLibrary(dwm);
}

bool ShsSystemPrefersDark(void)
{
    DWORD light = 1, size = sizeof light;
    LSTATUS rc = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &size);
    return rc == ERROR_SUCCESS ? light == 0 : true;
}

#else
  #include <sys/statvfs.h>
  #include <dirent.h>
  #include <pthread.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #ifdef __APPLE__
    #include <mach-o/dyld.h>
    #include <stdint.h>
  #endif

/* Runs a helper that prints a folder path on stdout. */
static bool RunPicker(const char *cmd, char *out, size_t cap)
{
    FILE *p = popen(cmd, "r");
    if (!p) return false;
    bool ok = fgets(out, (int)cap, p) != NULL;
    int rc = pclose(p);
    if (!ok || rc != 0) return false;
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    return n > 0;
}

bool ShsPickFolder(const char *title, char *out, size_t cap)
{
    (void)title;
  #ifdef __APPLE__
    return RunPicker("osascript -e 'POSIX path of (choose folder)' 2>/dev/null", out, cap);
  #else
    return RunPicker("zenity --file-selection --directory 2>/dev/null", out, cap) ||
           RunPicker("kdialog --getexistingdirectory \"$HOME\" 2>/dev/null", out, cap);
  #endif
}

double ShsFreeDiskGiB(const char *path)
{
    struct statvfs sv;
    if (statvfs(path, &sv) != 0) return -1.0;
    return (double)sv.f_bavail * (double)sv.f_frsize / (1024.0 * 1024.0 * 1024.0);
}

bool ShsSystemPrefersDark(void)
{
    char buf[128] = "";
  #ifdef __APPLE__
    /* prints "Dark" only when dark mode is on; exits non-zero otherwise */
    FILE *p = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
    if (!p) return true;
    bool dark = fgets(buf, sizeof buf, p) != NULL && strstr(buf, "Dark") != NULL;
    pclose(p);
    return dark;
  #else
    FILE *p = popen("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", "r");
    if (!p) return true;
    bool got = fgets(buf, sizeof buf, p) != NULL;
    pclose(p);
    if (got && strstr(buf, "prefer-dark")) return true;
    if (got && strstr(buf, "prefer-light")) return false;
    if (got && strstr(buf, "default")) return false;
    return true;
  #endif
}

/* Tray is Windows-only for now. */
bool ShsTraySupported(void)                  { return false; }
bool ShsTrayCreate(const char *tooltip)      { (void)tooltip; return false; }
bool ShsTrayActive(void)                     { return false; }
void ShsTraySetTooltip(const char *tooltip)  { (void)tooltip; }
void ShsTrayDestroy(void)                    {}
int  ShsTrayPoll(void)                       { return 0; }
bool ShsSingleInstance(void)                 { return true; }
void ShsTrimMemory(void)                     {}
void ShsSleepMs(unsigned ms)                 { usleep(ms * 1000u); }

/* ---- start with the system (macOS LaunchAgent / Linux autostart .desktop) ---- */

static bool ExePath(char *out, size_t cap)
{
  #ifdef __APPLE__
    uint32_t n = (uint32_t)cap;
    return _NSGetExecutablePath(out, &n) == 0;
  #else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return false;
    out[n] = 0;
    return true;
  #endif
}

bool ShsRelaunch(int argc, char **argv)
{
    char exe[1024];
    if (!ExePath(exe, sizeof exe)) return false;
    char **av = (char **)calloc((size_t)argc + 2, sizeof *av);
    if (!av) return false;
    av[0] = exe;
    for (int i = 1; i < argc; i++) av[i] = argv[i];
    av[argc] = (char *)"--restart";
    pid_t pid = fork();
    if (pid == 0) { setsid(); usleep(400000); execv(exe, av); _exit(1); }
    free(av);
    return pid > 0;
}

static bool AutostartFile(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
  #ifdef __APPLE__
    snprintf(out, cap, "%s/Library/LaunchAgents/com.sailhighsea.simplesync.plist", home);
  #else
    snprintf(out, cap, "%s/.config/autostart/sailhighsea-simplesync.desktop", home);
  #endif
    return true;
}

bool ShsAutostartEnabled(void)
{
    char f[600];
    return AutostartFile(f, sizeof f) && access(f, F_OK) == 0;
}

bool ShsAutostartSet(bool enable, bool startHidden)
{
    char f[600], exe[1024];
    if (!AutostartFile(f, sizeof f)) return false;
    if (!enable) return remove(f) == 0 || access(f, F_OK) != 0;
    if (!ExePath(exe, sizeof exe)) return false;

    char dir[600];
    snprintf(dir, sizeof dir, "%s", f);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdir(dir, 0755); }

    FILE *fp = fopen(f, "wb");
    if (!fp) return false;
  #ifdef __APPLE__
    fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>\n"
                "<key>Label</key><string>com.sailhighsea.simplesync</string>\n"
                "<key>ProgramArguments</key><array><string>%s</string>%s</array>\n"
                "<key>RunAtLoad</key><true/>\n</dict></plist>\n",
            exe, startHidden ? "<string>--minimized</string>" : "");
  #else
    fprintf(fp, "[Desktop Entry]\nType=Application\nName=SailHighSea SimpleSync\nExec=\"%s\"%s\nX-GNOME-Autostart-enabled=true\n",
            exe, startHidden ? " --minimized" : "");
  #endif
    return fclose(fp) == 0;
}
/* ---- folder watcher: polls a cheap signature (file count, newest mtime, total size) every 3 s ---- */

struct ShsWatch {
    char          folder[1024];
    pthread_t     thread;
    volatile int  changed, stop;
};

static void Signature(const char *dir, unsigned long long *count, long long *newest, unsigned long long *bytes, int depth)
{
    DIR *d = opendir(dir);
    if (!d || depth > 32) { if (d) closedir(d); return; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[2048];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        (*count)++;
        if ((long long)st.st_mtime > *newest) *newest = (long long)st.st_mtime;
        if (S_ISDIR(st.st_mode)) Signature(full, count, newest, bytes, depth + 1);
        else *bytes += (unsigned long long)st.st_size;
    }
    closedir(d);
}

static void *WatchThread(void *p)
{
    ShsWatch *w = (ShsWatch *)p;
    unsigned long long c0 = 0, b0 = 0; long long n0 = 0;
    Signature(w->folder, &c0, &n0, &b0, 0);
    while (!w->stop) {
        for (int i = 0; i < 30 && !w->stop; i++) usleep(100 * 1000);
        unsigned long long c = 0, b = 0; long long n = 0;
        Signature(w->folder, &c, &n, &b, 0);
        if (c != c0 || b != b0 || n != n0) { c0 = c; b0 = b; n0 = n; w->changed = 1; }
    }
    return NULL;
}

ShsWatch *ShsWatchStart(const char *folder)
{
    struct stat st;
    if (stat(folder, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
    ShsWatch *w = (ShsWatch *)calloc(1, sizeof *w);
    if (!w) return NULL;
    snprintf(w->folder, sizeof w->folder, "%s", folder);
    if (pthread_create(&w->thread, NULL, WatchThread, w) != 0) { free(w); return NULL; }
    return w;
}

bool ShsWatchTake(ShsWatch *w)
{
    if (!w || !w->changed) return false;
    w->changed = 0;
    return true;
}

void ShsWatchStop(ShsWatch *w)
{
    if (!w) return;
    w->stop = 1;
    pthread_join(w->thread, NULL);
    free(w);
}
bool ShsOpenFolder(const char *path)
{
    if (strchr(path, '\''))  return false;           /* keep the shell command trivially safe */
    char cmd[1200];
  #ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "open '%s' >/dev/null 2>&1 &", path);
  #else
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", path);
  #endif
    return system(cmd) == 0;
}

bool ShsOpenUrl(const char *url) { return ShsOpenFolder(url); }   /* open / xdg-open handle both */

bool ShsAutostartRepair(bool startHidden) { (void)startHidden; return false; }

void ShsRoundWindowCorners(void *nativeHandle) { (void)nativeHandle; }
#endif
