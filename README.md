# save-queue-foobar

A [foobar2000](https://www.foobar2000.org/) component that **saves and restores the
playback queue** to a plain-text file (`.fbq2k`). Useful for keeping a queue across
restarts, moving it between machines, or stashing a few "play next" lists.

### Auto-save

Stock foobar2000 clears the playback queue on exit, so the queue is lost on every
restart. This component removes that gap: it keeps `<profile>/autosave_queue.fbq2k`
in sync with the live queue — writing on every queue change — and restores it on the
next startup. The queue therefore survives not only clean exits but also crashes,
process kills, and power loss. Emptying the queue (or exiting with an empty queue)
clears the autosave file, so a deliberately empty queue stays empty.

You never need to touch the menu commands for this — they remain for explicit
export/import. They form a **File → Save Queue** menu group with three commands:

| Command | What it does |
| --- | --- |
| **Save playback queue…** | Writes the current queue to a `.fbq2k` file. |
| **Load playback queue (replace)…** | Clears the queue, then restores from a file. |
| **Append playback queue…** | Adds a file's entries to the end of the current queue. |

---

## ⚠️ Platform note (read first)

foobar2000 components are **Windows DLLs** built with the Microsoft Visual C++ toolchain
against the official foobar2000 SDK. They **cannot be built natively on Linux or macOS.**

This repository is set up so the build works in either of two ways:

1. **On a Windows machine** with Visual Studio 2022 (or 2019) — open the solution and build.
2. **Via GitHub Actions** — push to the repo and the `build` workflow produces the DLL on a
   Windows runner (see the *Build* section). You can download the artifact even with no
   local Windows setup.

The full official SDK is **vendored** under `third_party/foobar2000-sdk/`, so the repo is
self-contained — no SDK download is required at build time.

---

## Prerequisites (Windows build)

- **Visual Studio 2022** (recommended) with the *Desktop development with C++* workload.
  - The project targets toolset **v143**. VS 2019 users can switch to **v142** via the
    project's *Configuration Properties → General → Platform Toolset*.
- The **Windows 10/11 SDK** (the default with the C++ workload).

No other dependencies — `pfc`, `libPPUI`, the SDK, helpers, and the component-client bridge
are all included in the vendored SDK and build as part of the solution.

---

## Build

### Visual Studio (GUI)

1. Open **`save-queue-foobar.sln`**.
2. Pick the configuration that matches your foobar2000:
   - **`x64`** → for 64-bit foobar2000 (the default on modern Windows).
   - **`Win32`** (a.k.a. x86) → for 32-bit foobar2000.
   - `Release` for normal use, `Debug` for debugging.
3. **Build → Build Solution** (Ctrl+Shift+B).

The DLL lands at:

```
build\<Platform>\<Configuration>\foo_save_queue.dll
```

### Command line (MSBuild)

From a *Developer Command Prompt for VS*:

```bat
:: 64-bit release (most common)
msbuild save-queue-foobar.sln /p:Configuration=Release /p:Platform=x64

:: 32-bit release
msbuild save-queue-foobar.sln /p:Configuration=Release /p:Platform=Win32
```

### GitHub Actions

The [`.github/workflows/build.yml`](.github/workflows/build.yml) workflow builds both
`x86` and `x64` on every push/PR. Download the resulting `foo_save_queue-<plat>-<cfg>`
artifact from the run's **Artifacts** section.

---

## Install

Copy `foo_save_queue.dll` into one of:

- **`components\`** next to `foobar2000.exe` (installed for all users), or
- **`<profile>\user-components-x64\foo_save_queue\`** (per-user; survives app
  upgrades). The profile folder is `%APPDATA%\foobar2000` for a normal install, or a
  `profile\` folder inside the installation for a portable one (a
  `portable_mode_enabled` marker file is present next to `foobar2000.exe`). Put the
  DLL in its own `foo_save_queue\` subfolder.

Restart foobar2000. You should see **Save Queue** under the **File** menu.

> The bit-ness of the DLL **must** match the bit-ness of foobar2000 (x64 DLL into 64-bit
> foobar2000; Win32 DLL into 32-bit foobar2000).

> Release assets are per-architecture zips (`foo_save_queue-x64.zip` for 64-bit
> foobar2000, `foo_save_queue-x86.zip` for 32-bit); inside each is the DLL under its
> required name, `foo_save_queue.dll` — keep that name. The component self-checks its
> filename at startup, and a renamed DLL (e.g. one still carrying a `-x64` suffix)
> makes foobar2000 abort with *"Internal error - one or more of the installed
> components have been damaged; please run the foobar2000 installer again."* The same
> error appears if the extracted DLL is still blocked by Windows (downloads carry a
> Mark of the Web that propagates out of the zip) — unblock it (file Properties →
> Unblock, or `Unblock-File .\foo_save_queue.dll` in PowerShell) before installing.

---

## File format (`.fbq2k`)

A plain UTF-8 text file, one entry per line:

```
foobar2000 playback queue v1
<count>
<subsong-index><TAB><path>
<subsong-index><TAB><path>
...
```

`<subsong-index>` is the subsong number (0 for ordinary single-track files; >0 for
containers like cuesheets/chapters). Windows paths never contain a TAB or newline, so the
format is unambiguous. The `<count>` line is informational — the loader reads to EOF.

---

## Repository layout

```
save-queue-foobar/
├── save-queue-foobar.sln      # Visual Studio solution (component + SDK deps)
├── src/
│   └── foo_save_queue/        # ← your component
│       ├── main.cpp           # DECLARE_COMPONENT_VERSION, filename validation
│       ├── mainmenu.cpp       # File → Save Queue menu group + commands
│       ├── save_queue.cpp/.h  # queue ↔ file save/load logic
│       ├── stdafx.h / PCH.cpp # precompiled header
│       ├── foo_save_queue.rc  # version-info resource
│       └── foo_save_queue.vcxproj(.filters)
├── third_party/
│   └── foobar2000-sdk/        # official foobar2000 SDK (vendored, unmodified)
├── docs/ARCHITECTURE.md       # how the build graph & APIs fit together
└── .github/workflows/build.yml
```

---

## Licensing

- **This component's source** (everything under `src/`): see `LICENSE`.
- **The foobar2000 SDK** (under `third_party/foobar2000-sdk/`) is © Peter Pawlowski and is
  licensed under the terms in
  [`third_party/foobar2000-sdk/sdk-license.txt`](third_party/foobar2000-sdk/sdk-license.txt).
  `pfc` and `libPPUI` carry their own, more permissive licenses (see their folders).
- "foobar2000" is a trademark of Peter Pawlowski. This project is not affiliated with or
  endorsed by the foobar2000 author.
