# Architecture

How the foobar2000 component build is wired together, and which SDK APIs the
component uses.

## Why it only builds on Windows

foobar2000 loads components as **DLLs** (`foo_*.dll`) placed in its `components\`
or `user-components\` folder. The official foobar2000 SDK is built for the
Microsoft Visual C++ toolchain: the headers use ATL (`<atlbase.h>`, …), Win32,
and the component ABI is MSVC-specific. The SDK ships only Visual Studio
(`.vcxproj`) and Xcode project files — there is **no native Linux toolchain**,
and cross-compiling with MinGW/clang against this SDK is unsupported and fragile
(the ATL/WTL UI helpers in particular do not build).

So: **build on Windows with MSVC, or via a Windows CI runner.** See `README.md`.

## Build graph

`foo_save_queue.dll` is a `DynamicLibrary` that depends on five **static-library**
projects from the vendored SDK. Visual Studio builds the whole chain from the
single solution:

```
foo_save_queue.dll            (our component)
   ├── pfc                    (Peter's Foundation Classes — core containers/strings)
   ├── foobar2000_SDK         (service interfaces: playlist, metadb, filesystem, …)
   ├── foobar2000_sdk_helpers (higher-level helpers built on the SDK)
   ├── foobar2000_component_client  (the mandatory bridge every component links)
   ├── libPPUI                (UI helper library)
   └── shared-<Platform>.lib  (PREBUILT import lib — ships with the SDK)
```

`shared-*.lib` is an **import library** for `shared.dll`, which is part of
foobar2000 itself (it ships with the player). That's why the solution does **not**
build the `shared` project — the import libs (`shared-Win32.lib`,
`shared-x64.lib`, `shared-ARM64EC.lib`) are included in the SDK and our `.vcxproj`
links the right one via `shared-$(Platform).lib`.

### Include paths

Our component lives at `src/foo_save_queue/`. To resolve the SDK includes exactly
the way the official sample does, the project sets two include roots:

- `..\..\third_party\foobar2000-sdk\foobar2000` → `<SDK/…>`, `<helpers/…>`, `<shared/…>`
- `..\..\third_party\foobar2000-sdk` → `<pfc/…>`, `<libPPUI/…>`

### Configuration mapping in the solution

| Solution config | our / SDK / helpers / component_client / libPPUI | pfc |
| --- | --- | --- |
| `Debug\|x86`   | `Debug\|Win32`   | `Debug FB2K\|Win32`   |
| `Release\|x86` | `Release\|Win32` | `Release FB2K\|Win32` |
| `Debug\|x64`   | `Debug\|x64`     | `Debug FB2K\|x64`     |
| `Release\|x64` | `Release\|x64`   | `Release FB2K\|x64`   |

`pfc` uses its own `FB2K` configuration names; the others use the standard
`Debug`/`Release`. The project GUIDs in `save-queue-foobar.sln` match the
`<ProjectGuid>` in each SDK `.vcxproj` exactly.

### Target foobar2000 version

`third_party/foobar2000-sdk/foobar2000/SDK/foobar2000-versions.h` controls which
API level the component is built against (`FOOBAR2000_TARGET_VERSION`). On Windows
it defaults to **80** (the foobar2000 1.5/1.6 API), which is deliberately the
widest-compatible choice: the resulting DLL loads in foobar2000 **1.5, 1.6 and 2.x**.

This component only uses APIs that have existed since 1.x, so the default is ideal.
If you ever need 2.0-only features, edit that header and uncomment the
`#define FOOBAR2000_TARGET_VERSION 81` line (and expect the DLL to then require
foobar2000 2.0+).

## Component anatomy

A foobar2000 component is a normal C++ DLL that, at global scope, registers
**services** with the player via factory objects. Everything else (no `DllMain`
boilerplate needed) is handled by `foobar2000_component_client`.

| File | Registers / does |
| --- | --- |
| `main.cpp` | `DECLARE_COMPONENT_VERSION` (mandatory metadata), `VALIDATE_COMPONENT_FILENAME`, `FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE`. |
| `mainmenu.cpp` | A `mainmenu_group_popup_factory` ("Save Queue" under File) + a `mainmenu_commands` implementation with three commands. Each command's `execute()` calls into `save_queue.cpp`. |
| `save_queue.cpp` | Reads/writes `.fbq2k` files; talks to the queue via `playlist_manager` and resolves tracks via `metadb`. Also implements the autosave (`AutoSaveQueue`/`AutoLoadQueue`), which keeps `<profile>/autosave_queue.fbq2k` in sync with the live queue (content-deduplicated, atomic writes via `filesystem::rewrite_file`). |
| `auto_save_hooks.cpp` | Registers the autosave services: an `init_stage_callback` on `init_stages::after_ui_init` that queues the startup restore onto the main thread, an `initquit` for the save-on-exit, and a `playback_queue_callback` that schedules an autosave whenever the queue changes. |

## SDK APIs used

**Queue access** — `SDK/playlist.h`, through `playlist_manager::get()`:

```cpp
playlist_manager::get()->queue_is_active();        // any items queued?
playlist_manager::get()->queue_get_contents(list); // snapshot of t_playback_queue_item
playlist_manager::get()->queue_flush();            // clear the queue
playlist_manager::get()->queue_add_item(handle);   // enqueue a track
```

Each `t_playback_queue_item` carries a `metadb_handle_ptr m_handle`. From a handle we
get the identity we persist: `m_handle->get_path()` and `m_handle->get_subsong_index()`.

**Resolving a track back from a saved path** — `SDK/metadb.h`:

```cpp
metadb_handle_ptr h = metadb::get()->handle_create(path, subsong);
```

**File I/O** — `SDK/filesystem.h` + `SDK/file.h`:

```cpp
auto fs  = filesystem::get(path);
auto out = fs->openWriteNew(path, fb2k::noAbort, 1.0);   // CREATE_ALWAYS semantics
out->write(buf, len, fb2k::noAbort);

auto in  = fs->openRead(path, fb2k::noAbort, 1.0);
t_filesize sz = in->get_size(fb2k::noAbort);
in->read_object(buf, (t_size)sz, fb2k::noAbort);
```

The 1.0 timeout retries for up to one second on sharing violations. We also call
`file_lock_manager::get()->acquire_write(path, abort)` before writing, which is the
documented way to cooperate with foobar2000 when the target path is a file being played.

**Open/Save dialog** — `shared/shared.h`:

```cpp
uGetOpenFileName(core_api::get_main_window(), "…|*.fbq2k|All files|*.*",
                 0, "fbq2k", "Title", nullptr, pathOut, /*bSave=*/TRUE);
```

The last `BOOL` selects save (TRUE) vs open (FALSE).

**User feedback** — `SDK/popup_message.h` (`popup_message::g_show`, `g_complain`) and
`SDK/console.h` (`FB2K_console_formatter() << …`).

## Threading

All `playlist_manager`, `metadb`, and UI calls in this component happen on the **main
app thread**. Menu commands run there by definition. The autosave hooks defer work with
`fb2k::inMainThread` so file I/O never happens inside the core's queue-change dispatch
(`playback_queue_callback::on_changed`); a content snapshot makes no-change notifications
a no-op. Startup restore runs from `init_stages::after_ui_init`, and autosaves are gated
on the restore having been attempted so a short session cannot delete the previous
session's file. The file reads/writes are quick local-file operations using
`fb2k::noAbort`; if you later add large/remote operations, move them to a
`threaded_process_callback` and pass a real `abort_callback`.
