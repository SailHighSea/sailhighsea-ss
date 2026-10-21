#ifndef SHS_PLATFORM_H
#define SHS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

/* Native "choose folder" dialog (blocks until closed). Writes a UTF-8 path to `out`.
   Returns false if cancelled or no dialog is available on this system. */
bool ShsPickFolder(const char *title, char *out, size_t cap);

/* Free space in GiB on the volume holding `path`; negative if unknown. */
double ShsFreeDiskGiB(const char *path);

/* Opens a folder in the system file manager (Explorer / Finder / xdg-open). Returns false on failure. */
bool ShsOpenFolder(const char *path);

/* Opens a web address (https://...) in the default browser. */
bool ShsOpenUrl(const char *url);

/* True if the OS is set to a dark app theme. Defaults to true when it cannot tell. */
bool ShsSystemPrefersDark(void);

/* Asks the OS to round the corners of an undecorated top-level window (Windows 11 via DWM;
   no-op elsewhere or on older Windows). `nativeHandle` is raylib's GetWindowHandle(). */
void ShsRoundWindowCorners(void *nativeHandle);

/* ---- system tray (Windows only; elsewhere ShsTraySupported() is false and the rest are no-ops) ---- */
#define SHS_TRAY_SHOW      1   /* left click / "Open" */
#define SHS_TRAY_SYNC_ALL  2   /* menu: Sync all */
#define SHS_TRAY_QUIT      4   /* menu: Quit */

bool ShsTraySupported(void);
bool ShsTrayCreate(const char *tooltip);     /* adds the notification-area icon; false on failure */
bool ShsTrayActive(void);
void ShsTraySetTooltip(const char *tooltip);
void ShsTrayDestroy(void);
/* Windows: true for the first copy; a second copy gets false after asking the first to show its window
   (via the SHS_TRAY_SHOW event) and should exit. Other platforms always return true. */
bool ShsSingleInstance(void);

/* Windows: hands unused memory pages back to the OS (shrinks the working set shown in Task Manager); they are paged in
   again on demand. Call when the app has been idle for a while. No-op elsewhere. */
void ShsTrimMemory(void);

/* Starts a fresh copy of this program (same arguments plus --restart, which makes it wait for this copy to exit). Returns false on failure. */
bool ShsRelaunch(int argc, char **argv);

/* Sleeps without spinning (unlike raylib's WaitTime, which busy-waits for its last few percent). */
void ShsSleepMs(unsigned ms);
int  ShsTrayPoll(void);                     /* returns and clears the SHS_TRAY_* events since the last call */

/* ---- folder watcher: tells the app when anything under a folder changed ---- */
typedef struct ShsWatch ShsWatch;
ShsWatch *ShsWatchStart(const char *folder);   /* NULL if the folder cannot be watched */
bool      ShsWatchTake(ShsWatch *w);           /* true once per burst of changes since the last call */
void      ShsWatchStop(ShsWatch *w);

/* ---- removable drives ---- */
/* The volume that holds `path`: "E:\" on Windows, "\\server\share", "/Volumes/NAME", "/media/user/NAME", "/mnt/NAME".
   False when the path is not on a volume that can come and go (e.g. a plain "/home/..." folder). */
bool ShsVolumeRoot(const char *path, char *out, size_t cap);
/* True while the volume is connected and readable. Cheap enough to call every couple of seconds. */
bool ShsVolumeMounted(const char *root);
/* Roots of the drives that are connected right now: "E:\" on Windows (removable and fixed drives), the mount points under
   /media, /run/media and /mnt on Linux, /Volumes/NAME on macOS. Returns how many were written (at most `max`). */
#define SHS_VOLUME_PATH 260
int  ShsListVolumes(char (*roots)[SHS_VOLUME_PATH], int max);

/* ID marker: a small hidden file ".simplesync-id" in a target folder naming the sync tasks it belongs to ("id=<hex>" lines).
   A USB sync task finds its stick again by looking for its id in the same folder on every connected drive, whatever
   drive letter or mount point the stick got this time. */
bool ShsMarkerHas(const char *folder, const char *id);
bool ShsMarkerRemove(const char *folder, const char *id);   /* takes the id out again (a task was deleted); the file goes when no id is left */
bool ShsMarkerAdd(const char *folder, const char *id);      /* creates the file if needed and adds the id; true if it is there afterwards */


/* ---- start with the system ---- */
bool ShsAutostartEnabled(void);
bool ShsAutostartSet(bool enable, bool startHidden);
/* If the start-up entry exists but its program file is gone (for example an old numbered build was deleted), re-register
   the running program instead. Returns true when it was repaired. A working entry is never touched. */
bool ShsAutostartRepair(bool startHidden);   /* registers the running program (Run key / LaunchAgent / .desktop) */

#endif /* SHS_PLATFORM_H */
