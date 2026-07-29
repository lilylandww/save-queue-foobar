// Precompiled header for foo_save_queue.
//
// This single umbrella pulls in the entire foobar2000 SDK
// (playlist/queue, metadb, filesystem, menus, popups, console, ...) as well
// as the ATL-based helpers. It mirrors the foo_sample PCH that ships with the
// official SDK so the build behaves identically.
#ifdef __cplusplus
#include <helpers/foobar2000+atl.h>
#endif

#ifdef __OBJC__
#include <Cocoa/Cocoa.h>
#endif
