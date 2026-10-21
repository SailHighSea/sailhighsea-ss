#ifndef SHS_UPDATER_H
#define SHS_UPDATER_H

#include <stdbool.h>
#include <stddef.h>

/* Asks GitHub for the newest release on a background thread: the newest stable one, or with `includeDev` the newest of all, dev pre-releases included. Returns false if a check is already running or the
   thread could not be started. Nothing is downloaded or installed: the app only tells the user a newer version exists. */
bool ShsUpdateCheckStart(bool includeDev);

/* Returns 0 while the check is running (or none was started), 1 once it finished (`tag` and `url` hold the latest
   release's tag, e.g. "v0.0.4", and its web page) and -1 if it failed (offline, no release yet). 1 and -1 are reported once. */
int  ShsUpdateCheckPoll(char *tag, size_t tagCap, char *url, size_t urlCap);

/* True when version `candidate` ("v0.0.4" or "0.0.4") is newer than `current` ("0.0.3", "0.0.4-dev-2").
   A pre-release build of 0.0.4 counts as older than the final 0.0.4. */
bool ShsVersionNewer(const char *candidate, const char *current);

/* Like ShsVersionNewer, but only the version number counts: 0.0.4-dev-9 vs 0.0.4 is not an update either way. Used when dev builds are not wanted. */
bool ShsVersionCoreNewer(const char *candidate, const char *current);

#endif /* SHS_UPDATER_H */
