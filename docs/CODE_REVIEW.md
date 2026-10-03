# Code review: auto-save queue

Review of uncommitted changes: `auto_save_hooks.cpp`, `save_queue.cpp` / `save_queue.h`, README, release workflow, and project files.

---

## Summary

The autosave feature is the right shape (deferred writes, startup restore, profile file), but several paths can **lose the queue** the feature is meant to preserve. Restore is deferred; shutdown and the change callback still treat an empty in-memory queue as “user cleared it” and delete `autosave_queue.fbq2k`. The write path also truncates that file in place, so a crash mid-save can leave a partial file that the next launch will load.

---

## Critical: data loss

### Quit before restore timer runs deletes the previous queue

`initquit::on_init` only arms a 100 ms timer. `on_quit` always calls `AutoSaveQueue()`, and an empty live queue takes the delete branch:

```cpp
// auto_save_hooks.cpp
void on_quit() override {
    KillTimer(nullptr, kRestoreTimerId);
    AutoSaveQueue();
}

// save_queue.cpp
if (pm->queue_is_active()) {
    SaveQueueToFilePath(autoPath, false);
} else {
    try {
        if (filesystem::g_exists(autoPath, abort)) {
            filesystem::g_remove(autoPath, abort);
        }
    } catch (...) {}
}
```

At startup the queue is empty until the timer fires. Closing foobar in that window (or any time `get_main_window()` stays null and the timer keeps polling) **removes the file from the previous session**. The same delete happens if `on_changed` runs before load: the deferred save sees an empty queue and erases the file the timer is about to read.

**Recommendation:** Ignore queue-change saves until a restore attempt has finished. On quit before that attempt, **do not** delete the autosave file.

### Autosave truncates destination (crash window)

`SaveQueueToFilePath` uses `openWriteNew` (create-always). A crash, kill, or power loss between truncate and the final write leaves a short file. The loader does not validate the magic line strictly for early abort; it skips the first two lines and enqueues whatever parses. A partial file becomes the new queue, and the next autosave commits it.

The SDK’s `filesystem::rewrite_file` writes `path.new.tmp`, flushes, and `replace_file`s over the original (see `filesystem.cpp`). Use that for the autosave path. Manual saves have the same hole with a smaller blast radius.

### Failed deletes are silent

`catch (...)` around remove swallows errors. If removal fails, the old file remains and the next startup restores a queue the user thought they cleared. Log failures to the console (same pattern as other autosave errors).

---

## Restore timing and queue badges

### Timer does not wait for playlist UI

SDK docs: `initquit::on_init` runs **after** the main window exists. The first timer tick (~100 ms later) usually sees non-null `get_main_window()` and loads immediately. The null check does not wait for playlist views. Slow layout can still subscribe after load — the missed-badge case the comment describes.

**Recommendation:** Consider `init_stages::after_ui_init` or an extra readiness signal beyond `get_main_window()`.

### Playlist walk attaches badges to the wrong playlist

`playlist_find_item` is first-match in playlist index order. If an earlier playlist (library autoplaylist, “all music”) also contains the track, the badge attaches there while the playlist the user queued from shows nothing. Duplicate copies of one track in one playlist collapse onto the first row.

The SDK marks `playlist_find_item` as a full playlist walk — main-thread cost for a long queue against large playlists. The “handful of comparisons” comment is inaccurate.

The on-disk format only has path + subsong; original `t_playback_queue_item::m_playlist` / `m_item` cannot be recovered exactly. Searching the **active** playlist first, then others, would better match user intent. Persisting playlist name (not index) would survive restart better than playlist order.

`LoadQueueFromFilePath(..., true)` also wipes anything queued in the delay before restore runs.

---

## What looks good

- Deferring writes with `fb2k::inMainThread` is correct: `playback_queue_callback::on_changed` runs inside core queue dispatch; `inMainThread` is FIFO and does not run inline, so restore’s per-item notifications can collapse into one save of the final queue.
- Clearing `m_pending` before `AutoSaveQueue()` is the right coalesce order; a change during the write schedules one more save.
- `FB2K_SERVICE_FACTORY` registration and `auto_save_hooks.cpp` in the vcxproj are wired correctly.

### Minor: shutdown race

The lambda captures `this` on a process-lifetime service. A callback posted just before `on_quit` might still run while teardown proceeds. A shutdown flag checked at the start of the lambda would close that gap.

---

## Documentation gaps

- **README:** After the auto-save section, text ends with “they remain for explicit export/import:” then jumps to “It adds a File → Save Queue menu group” — broken paragraph flow.
- **docs/ARCHITECTURE.md:** Still states all queue access is from menu commands on the main thread and that `main_thread_callback` is not needed; omits `auto_save_hooks.cpp` from layout.
- **main.cpp:** Component version string still describes only menu save/load, not auto-save.

---

## Suggested test plan

1. Queue several tracks, exit immediately on cold start (< 1 s) — verify `autosave_queue.fbq2k` still exists and restores on next launch.
2. Kill foobar during autosave (e.g. while rapidly reordering queue) — verify next launch does not load a truncated queue.
3. Queue from playlist B while the same file exists in playlist A (lower index) — verify queue badge appears on the intended row.
4. Clear queue, confirm autosave file removed; if delete fails (simulate locked file), confirm behavior/logging.
