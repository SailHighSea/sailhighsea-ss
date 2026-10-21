#include "updater.h"

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE
  #define _DARWIN_C_SOURCE
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_HOST  "api.github.com"
#define API_STABLE "/repos/SailHighSea/sailhighsea-ss/releases/latest"
#define API_ALL    "/repos/SailHighSea/sailhighsea-ss/releases?per_page=5"   /* newest first, pre-releases included */
#define BODY_MAX  (96 * 1024)

/* ---- version compare ------------------------------------------------------ */

/* "v0.0.4-dev-2" -> {0,0,4}, pre = true, dev = 2. A final release has pre = false (and counts as newer than its dev builds). */
static void ParseVersion(const char *v, int out[3], bool *pre, int *dev)
{
    out[0] = out[1] = out[2] = 0;
    *pre = false;
    *dev = 0;
    if (*v == 'v' || *v == 'V') v++;
    for (int i = 0; i < 3; i++) {
        out[i] = atoi(v);
        while (*v >= '0' && *v <= '9') v++;
        if (*v == '.') v++; else break;
    }
    if (*v == '-') {
        *pre = true;
        const char *d = strrchr(v, '-');
        if (d) *dev = atoi(d + 1);
    }
}

bool ShsVersionNewer(const char *candidate, const char *current)
{
    int a[3], b[3], ad, bd;
    bool ap, bp;
    ParseVersion(candidate, a, &ap, &ad);
    ParseVersion(current, b, &bp, &bd);
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    if (ap != bp) return bp;                       /* the final release beats its dev builds */
    return ap && ad > bd;
}

bool ShsVersionCoreNewer(const char *candidate, const char *current)
{
    int a[3], b[3], d;
    bool p;
    ParseVersion(candidate, a, &p, &d);
    ParseVersion(current, b, &p, &d);
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

/* ---- tiny JSON string lookup (first "key": "value") ----------------------- */

static bool JsonString(const char *body, const char *key, char *out, size_t cap)
{
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return n > 0;
}

/* ---- HTTPS GET ------------------------------------------------------------- */

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winhttp.h>

static char *HttpGet(bool includeDev)
{
    char *body = NULL;
    HINTERNET ses = WinHttpOpen(L"SailHighSea-SimpleSync", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!ses) return NULL;
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 8000);
    HINTERNET con = WinHttpConnect(ses, L"" API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", includeDev ? L"" API_ALL : L"" API_STABLE, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(req, NULL)) {
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            body = (char *)malloc(BODY_MAX + 1);
            size_t len = 0;
            DWORD got = 0;
            while (body && len < BODY_MAX && WinHttpReadData(req, body + len, (DWORD)(BODY_MAX - len), &got) && got) len += got;
            if (body) body[len] = 0;
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return body;
}

typedef HANDLE UpThread;
static DWORD WINAPI UpMain(LPVOID arg);
static bool UpSpawn(void) { HANDLE h = CreateThread(NULL, 0, UpMain, NULL, 0, NULL); if (!h) return false; CloseHandle(h); return true; }
#else
  #include <pthread.h>

/* No HTTP library is linked on macOS / Linux: curl ships with both. */
static char *HttpGet(bool includeDev)
{
    FILE *p = popen(includeDev ? "curl -fsSL --max-time 10 -H \"Accept: application/vnd.github+json\" -A SailHighSea-SimpleSync \"https://" API_HOST API_ALL "\" 2>/dev/null"
                              : "curl -fsSL --max-time 10 -H \"Accept: application/vnd.github+json\" -A SailHighSea-SimpleSync "
                    "\"https://" API_HOST API_STABLE "\" 2>/dev/null", "r");
    if (!p) return NULL;
    char *body = (char *)malloc(BODY_MAX + 1);
    size_t len = 0;
    while (body && len < BODY_MAX) {
        size_t n = fread(body + len, 1, BODY_MAX - len, p);
        if (n == 0) break;
        len += n;
    }
    if (body) body[len] = 0;
    if (pclose(p) != 0) { free(body); return NULL; }
    return body;
}

static void *UpMain(void *arg);
static bool UpSpawn(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, UpMain, NULL) != 0) return false;
    pthread_detach(t);
    return true;
}
#endif

/* ---- background check ------------------------------------------------------ */

enum { UP_IDLE = 0, UP_RUNNING, UP_OK, UP_FAILED };
static atomic_int g_state = UP_IDLE;
static bool       g_includeDev;                   /* set before the thread starts */
static char       g_tag[64], g_url[256];          /* written by the thread before it publishes UP_OK */

#ifdef _WIN32
static DWORD WINAPI UpMain(LPVOID arg)
#else
static void *UpMain(void *arg)
#endif
{
    (void)arg;
    char *body = HttpGet(g_includeDev);
    char tag[64], url[256];
    bool ok = body && JsonString(body, "tag_name", tag, sizeof tag) && JsonString(body, "html_url", url, sizeof url)
              && !strncmp(url, "https://github.com/", 19);                    /* it will be opened in the browser */
    free(body);
    if (ok) { snprintf(g_tag, sizeof g_tag, "%s", tag); snprintf(g_url, sizeof g_url, "%s", url); }
    atomic_store(&g_state, ok ? UP_OK : UP_FAILED);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

bool ShsUpdateCheckStart(bool includeDev)
{
    int idle = UP_IDLE;
    if (!atomic_compare_exchange_strong(&g_state, &idle, UP_RUNNING)) return false;
    g_includeDev = includeDev;
    if (!UpSpawn()) { atomic_store(&g_state, UP_IDLE); return false; }
    return true;
}

int ShsUpdateCheckPoll(char *tag, size_t tagCap, char *url, size_t urlCap)
{
    int s = atomic_load(&g_state);
    if (s != UP_OK && s != UP_FAILED) return 0;
    if (s == UP_OK) { snprintf(tag, tagCap, "%s", g_tag); snprintf(url, urlCap, "%s", g_url); }
    atomic_store(&g_state, UP_IDLE);
    return s == UP_OK ? 1 : -1;
}
