#include "platform.h"

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
  #define _DEFAULT_SOURCE
  #define _DARWIN_C_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARKER_NAME ".simplesync-id"

static bool IsSep(char c) { return c == '/' || c == '\\'; }

/* <folder>/.simplesync-id, with the separator the folder already uses. */
static void MarkerPath(char *out, size_t cap, const char *folder)
{
    size_t n = strlen(folder);
    bool sep = n && IsSep(folder[n - 1]);
    snprintf(out, cap, "%s%s%s", folder, sep ? "" : (strchr(folder, '\\') ? "\\" : "/"), MARKER_NAME);
}

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

#define MODE(s) L##s
static FILE *OpenUtf8(const char *path, const wchar_t *mode)
{
    wchar_t w[1100];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) <= 0) return NULL;
    return _wfopen(w, mode);
}

bool ShsVolumeRoot(const char *path, char *out, size_t cap)
{
    if (!path || cap < 8) return false;
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
        snprintf(out, cap, "%c:\\", path[0]);
        return true;
    }
    if (IsSep(path[0]) && IsSep(path[1])) {                   /* \\server\share */
        const char *p = path + 2;
        int parts = 0;
        size_t n = 2;
        snprintf(out, cap, "\\\\");
        for (; *p && n + 2 < cap; p++) {
            if (IsSep(*p)) { if (++parts == 2) break; out[n++] = '\\'; }
            else           { out[n++] = *p; }
        }
        out[n] = 0;
        return parts >= 1 && n > 3;
    }
    return false;
}

bool ShsVolumeMounted(const char *root)
{
    if (!root || !root[0]) return false;
    if (root[1] == ':') {
        char letter = root[0];
        if (letter >= 'a' && letter <= 'z') letter = (char)(letter - 32);
        if (letter < 'A' || letter > 'Z') return false;
        if (!(GetLogicalDrives() & (1u << (letter - 'A')))) return false;
        wchar_t w[4] = { (wchar_t)letter, L':', L'\\', 0 };
        if (GetDriveTypeW(w) == DRIVE_REMOTE) return true;     /* no probing: a dead share would stall */
        return GetVolumeInformationW(w, NULL, 0, NULL, NULL, NULL, NULL, 0) != 0;   /* fails for an empty card reader slot */
    }
    wchar_t w[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, root, -1, w, 1024) <= 0) return false;
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}

int ShsListVolumes(char (*roots)[SHS_VOLUME_PATH], int max)
{
    int n = 0;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26 && n < max; i++) {
        if (!(mask & (1u << i))) continue;
        wchar_t w[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        UINT type = GetDriveTypeW(w);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED) continue;      /* no probing of network or optical drives */
        snprintf(roots[n++], SHS_VOLUME_PATH, "%c:\\", 'A' + i);
    }
    return n;
}

#else
  #include <dirent.h>
  #include <sys/stat.h>

static FILE *OpenUtf8(const char *path, const char *mode) { return fopen(path, mode); }
  #define MODE(s) s

/* Where removable drives get mounted, and how many path components after the prefix name the volume. */
bool ShsVolumeRoot(const char *path, char *out, size_t cap)
{
    static const struct { const char *prefix; int parts; } MOUNTS[] = {
        { "/Volumes/", 1 }, { "/media/", 2 }, { "/run/media/", 2 }, { "/mnt/", 1 },
    };
    if (!path) return false;
    for (size_t m = 0; m < sizeof MOUNTS / sizeof MOUNTS[0]; m++) {
        size_t pl = strlen(MOUNTS[m].prefix);
        if (strncmp(path, MOUNTS[m].prefix, pl) != 0) continue;
        const char *p = path + pl;
        int parts = 0;
        size_t n = pl;
        if (n + 1 >= cap) return false;
        memcpy(out, path, pl);
        for (; *p && n + 1 < cap; p++) {
            if (*p == '/') { if (++parts == MOUNTS[m].parts) break; }
            out[n++] = *p;
        }
        out[n] = 0;
        return n > pl && parts + (*p ? 0 : 1) >= MOUNTS[m].parts;
    }
    return false;
}

bool ShsVolumeMounted(const char *root)
{
    struct stat st;
    return root && root[0] && stat(root, &st) == 0 && S_ISDIR(st.st_mode);
}

  #ifdef __APPLE__

int ShsListVolumes(char (*roots)[SHS_VOLUME_PATH], int max)
{
    int n = 0;
    DIR *d = opendir("/Volumes");
    if (!d) return 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(roots[n++], SHS_VOLUME_PATH, "/Volumes/%s", e->d_name);
    }
    closedir(d);
    return n;
}

  #else   /* Linux: the mount points under the usual removable-drive folders, from /proc/self/mountinfo */

/* "\040" style escapes used by /proc/self/mountinfo for spaces and other odd characters. */
static void Unescape(char *s)
{
    char *o = s;
    while (*s) {
        if (s[0] == '\\' && s[1] >= '0' && s[1] <= '7' && s[2] >= '0' && s[2] <= '7' && s[3] >= '0' && s[3] <= '7') {
            *o++ = (char)(((s[1] - '0') << 6) | ((s[2] - '0') << 3) | (s[3] - '0'));
            s += 4;
        } else *o++ = *s++;
    }
    *o = 0;
}

int ShsListVolumes(char (*roots)[SHS_VOLUME_PATH], int max)
{
    int n = 0;
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f) return 0;
    char line[4608];
    while (n < max && fgets(line, sizeof line, f)) {
        char mp[1024];
        if (sscanf(line, "%*s %*s %*s %*s %1023s", mp) != 1) continue;
        Unescape(mp);
        if (strncmp(mp, "/media/", 7) && strncmp(mp, "/run/media/", 11) && strncmp(mp, "/mnt/", 5)) continue;
        bool dup = false;
        for (int i = 0; i < n; i++) if (!strcmp(roots[i], mp)) dup = true;
        if (!dup) snprintf(roots[n++], SHS_VOLUME_PATH, "%s", mp);
    }
    fclose(f);
    return n;
}

  #endif
#endif

/* ---- ID marker -------------------------------------------------------------------------------------------------- */

bool ShsMarkerHas(const char *folder, const char *id)
{
    if (!folder || !folder[0] || !id || !id[0]) return false;
    char path[1200], line[128];
    MarkerPath(path, sizeof path, folder);
    FILE *f = OpenUtf8(path, MODE("rb"));
    if (!f) return false;
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!strncmp(line, "id=", 3) && !strcmp(line + 3, id)) found = true;
    }
    fclose(f);
    return found;
}

bool ShsMarkerAdd(const char *folder, const char *id)
{
    if (!folder || !folder[0] || !id || !id[0]) return false;
    if (ShsMarkerHas(folder, id)) return true;
    char path[1200];
    MarkerPath(path, sizeof path, folder);
    bool isNew = false;
    FILE *f = OpenUtf8(path, MODE("rb"));
    if (f) fclose(f); else isNew = true;
#ifdef _WIN32
    wchar_t w[1100];
    if (!isNew && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) > 0) SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);   /* hidden files cannot be reopened for writing otherwise */
#endif
    f = OpenUtf8(path, MODE("ab"));
    if (!f) return false;
    if (isNew) fputs("SailHighSea SimpleSync target marker. Do not delete: it tells SimpleSync which USB drive this folder belongs to.\n", f);
    fprintf(f, "id=%s\n", id);
    bool ok = fclose(f) == 0;
#ifdef _WIN32
    if (ok && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) > 0) SetFileAttributesW(w, FILE_ATTRIBUTE_HIDDEN);
#endif
    return ok;
}

bool ShsMarkerRemove(const char *folder, const char *id)
{
    if (!folder || !folder[0] || !id || !id[0] || !ShsMarkerHas(folder, id)) return true;      /* nothing of ours there */
    char path[1200], line[128], keep[16][128];
    MarkerPath(path, sizeof path, folder);
    FILE *f = OpenUtf8(path, MODE("rb"));
    if (!f) return true;
    int kept = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!strncmp(line, "id=", 3) && !strcmp(line + 3, id)) continue;                      /* drop this task's id */
        if (!strncmp(line, "id=", 3) && kept < 16) snprintf(keep[kept++], sizeof keep[0], "%s", line);
    }
    fclose(f);
#ifdef _WIN32
    wchar_t w[1100];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) > 0) SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);
#endif
    if (kept == 0) {                                                                           /* no task left: the marker file goes too */
#ifdef _WIN32
        return DeleteFileW(w) != 0;
#else
        return remove(path) == 0;
#endif
    }
    f = OpenUtf8(path, MODE("wb"));
    if (!f) return false;
    fputs("SailHighSea SimpleSync target marker. Do not delete: it tells SimpleSync which USB drive this folder belongs to.\n", f);
    for (int i = 0; i < kept; i++) fprintf(f, "%s\n", keep[i]);
    bool ok = fclose(f) == 0;
#ifdef _WIN32
    if (ok && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100) > 0) SetFileAttributesW(w, FILE_ATTRIBUTE_HIDDEN);
#endif
    return ok;
}
