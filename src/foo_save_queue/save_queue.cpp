#include "stdafx.h"
#include "save_queue.h"

#include <cstring> // memchr

namespace {

// ---------------------------------------------------------------------------
// On-disk format for a saved playback queue.
//
//   Line 1 : magic header (also acts as a version tag)
//   Line 2 : <item count>            (decimal, informational; we read to EOF)
//   Line N : <subsong_index><TAB><utf-8 path>
//
// Windows file paths never contain a TAB or a newline, so one entry per line
// with a TAB delimiter is unambiguous. The file is UTF-8 text.
// ---------------------------------------------------------------------------

constexpr const char* kQueueFileMagic = "foobar2000 playback queue v1";
constexpr const char* kQueueFileExt = "fbq2k";
constexpr const char* kFileDialogFilter =
    "Playback queue files (*.fbq2k)|*.fbq2k|All files (*.*)|*.*";

// 16 MiB sanity cap: a real queue file is a few kilobytes; anything bigger is
// almost certainly not one of our files.
constexpr t_filesize kMaxQueueFileBytes = 16 * 1024 * 1024;

void report_error(const char* what, const char* detail) {
    popup_message::g_complain(PFC_string_formatter() << what << "\n\n" << detail);
}

// Encode one queued item as "<subsong>\t<path>\r\n".
pfc::string8 format_handle(metadb_handle_ptr p_handle) {
    pfc::string8 line;
    line << pfc::format_uint(p_handle->get_subsong_index());
    line << "\t";
    line << p_handle->get_path();
    line << "\r\n";
    return line;
}

// Parse the subsong index that precedes the TAB on a data line. Returns true
// and fills p_out when at least one digit was consumed; the caller treats the
// remainder of the line (after the TAB) as the path.
bool parse_subsong(const char* p_begin, const char* p_tab, t_uint32& p_out) {
    t_uint32 value = 0;
    const char* p = p_begin;
    while (p < p_tab && *p >= '0' && *p <= '9') {
        value = value * 10u + static_cast<t_uint32>(*p - '0');
        ++p;
    }
    if (p == p_begin || p != p_tab) return false; // empty or has junk before TAB
    p_out = value;
    return true;
}

// Serialize the current queue into the on-disk text body. Returns false when
// the queue is empty (nothing to serialize).
bool build_queue_body(pfc::string8& p_out) {
    auto pm = playlist_manager::get();
    if (!pm->queue_is_active()) return false;

    pfc::list_t<t_playback_queue_item> items;
    pm->queue_get_contents(items);

    p_out.reset();
    p_out << kQueueFileMagic << "\r\n";
    p_out << pfc::format_uint(items.get_count()) << "\r\n";
    for (t_size i = 0; i < items.get_count(); ++i) {
        p_out << format_handle(items[i].m_handle);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void SaveQueueToFilePath(const char* path, bool show_popups) {
    try {
        auto pm = playlist_manager::get();

        if (!pm->queue_is_active()) {
            if (show_popups) {
                popup_message::g_show("The playback queue is empty; nothing to save.", "Save Queue");
            }
            return;
        }

        pfc::list_t<t_playback_queue_item> items;
        pm->queue_get_contents(items);
        const t_size count = items.get_count();

        // Build the whole body in memory first; queue files are tiny.
        pfc::string8 body;
        body << kQueueFileMagic << "\r\n";
        body << pfc::format_uint(count) << "\r\n";
        for (t_size i = 0; i < count; ++i) {
            body << format_handle(items[i].m_handle);
        }

        // Acquire a write lock so we cooperate with foobar2000 if the chosen
        // path happens to be a file currently being played.
        abort_callback& abort = fb2k::noAbort;
        auto lock = file_lock_manager::get()->acquire_write(path, abort);

        // rewrite_file = temp file + atomic replace; a crash mid-save cannot
        // leave a truncated queue file.
        filesystem::get(path)->rewrite_file(path, abort, 1.0, body.get_ptr(), body.get_length());

        FB2K_console_formatter()
            << "Save Queue: wrote " << count << " item(s) to " << file_path_display(path);

        if (show_popups) {
            popup_message::g_show(
                PFC_string_formatter()
                    << "Saved " << count << " queued item(s) to:\n" << file_path_display(path),
                "Save Queue");
        }
    } catch (const exception_aborted&) {
        throw;
    } catch (const std::exception& e) {
        if (show_popups) {
            report_error("Failed to save the playback queue.", e.what());
        } else {
            FB2K_console_formatter() << "Save Queue error: " << e.what();
        }
    }
}

// ---------------------------------------------------------------------------
void LoadQueueFromFilePath(const char* path, bool p_replace, bool show_popups) {
    try {
        // --- Read the entire file into memory. ---
        abort_callback& abort = fb2k::noAbort;
        auto in = filesystem::get(path)->openRead(path, abort, 1.0);

        const t_filesize size = in->get_size(abort);
        if (size == filesize_invalid || size > kMaxQueueFileBytes) {
            throw std::runtime_error("File is unreadable, empty, or too large to be a queue file.");
        }

        pfc::array_t<char> buffer;
        buffer.set_size(static_cast<t_size>(size) + 1);
        if (size > 0) {
            in->read_object(buffer.get_ptr(), static_cast<t_size>(size), abort);
        }
        buffer[static_cast<t_size>(size)] = '\0';

        // --- Parse line by line. ---
        const char* cursor = buffer.get_ptr();
        const char* end = cursor + size;

        // Skip a UTF-8 BOM if present.
        if (end - cursor >= 3 && static_cast<unsigned char>(cursor[0]) == 0xEF &&
            static_cast<unsigned char>(cursor[1]) == 0xBB &&
            static_cast<unsigned char>(cursor[2]) == 0xBF) {
            cursor += 3;
        }

        auto mdb = metadb::get();

        // Collect parsed handles first; only touch the live queue once parsing
        // succeeds so a corrupt file can't leave the queue half-cleared.
        pfc::list_t<metadb_handle_ptr> handles;

        t_size lineNo = 0;
        while (cursor < end) {
            ++lineNo;
            const char* nl = static_cast<const char*>(memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
            const char* lineEnd = nl ? nl : end;

            // Trim a trailing CR.
            const char* realEnd = lineEnd;
            if (realEnd > cursor && realEnd[-1] == '\r') --realEnd;

            // Lines 1 (magic) and 2 (count) are metadata, not data.
            const bool isDataLine = (lineNo > 2);
            if (isDataLine && realEnd > cursor) {
                const char* tab = static_cast<const char*>(memchr(cursor, '\t', static_cast<size_t>(realEnd - cursor)));
                if (tab && tab < realEnd) {
                    t_uint32 subsong = 0;
                    if (parse_subsong(cursor, tab, subsong)) {
                        // Path is everything after the TAB.
                        pfc::string8 filePath;
                        filePath.add_string(tab + 1, static_cast<t_size>(realEnd - (tab + 1)));

                        if (filePath.length() > 0) {
                            metadb_handle_ptr handle = mdb->handle_create(filePath, subsong);
                            handles.add_item(handle);
                        }
                    }
                }
            }

            cursor = nl ? nl + 1 : end;
        }

        if (handles.get_count() == 0) {
            if (show_popups) {
                popup_message::g_show(
                    PFC_string_formatter()
                        << "No queue entries were found in:\n" << file_path_display(path)
                        << "\n\nMake sure this is a file saved by the Save Queue component.",
                    "Save Queue");
            }
            return;
        }

        // --- Apply to the live queue. ---
        auto pm = playlist_manager::get();
        if (p_replace) {
            pm->queue_flush();
        }

        // Prefer playlist-based queue entries: the playlist UI renders queue
        // badges for entries tied to a playlist item, while raw-handle entries
        // (from files no longer in any playlist) may not show one. This is a
        // full-walk lookup per entry (the SDK marks playlist_find_item as
        // inefficient), so search the active playlist first - the one the user
        // most likely queued from - then the rest in index order. Note the
        // on-disk format only stores path + subsong, so which playlist the
        // entry was originally queued from cannot be recovered exactly.
        const t_size playlistCount = pm->get_playlist_count();
        const t_size activePlaylist = pm->get_active_playlist();
        for (t_size i = 0; i < handles.get_count(); ++i) {
            bool queuedInPlaylist = false;
            for (t_size order = 0; order < playlistCount && !queuedInPlaylist; ++order) {
                const t_size p = (order == 0) ? activePlaylist : (order <= activePlaylist ? order - 1 : order);
                if (p >= playlistCount) continue;
                t_size item = 0;
                if (pm->playlist_find_item(p, handles[i], item)) {
                    pm->queue_add_item_playlist(p, item);
                    queuedInPlaylist = true;
                }
            }
            if (!queuedInPlaylist) {
                pm->queue_add_item(handles[i]);
            }
        }

        FB2K_console_formatter()
            << "Save Queue: " << (p_replace ? "loaded" : "appended") << " "
            << handles.get_count() << " item(s) from " << file_path_display(path);

        if (show_popups) {
            popup_message::g_show(
                PFC_string_formatter()
                    << (p_replace ? "Loaded " : "Appended ") << handles.get_count()
                    << " queued item(s) from:\n" << file_path_display(path),
                "Save Queue");
        }
    } catch (const exception_aborted&) {
        throw;
    } catch (const std::exception& e) {
        if (show_popups) {
            report_error(
                p_replace ? "Failed to load the playback queue." : "Failed to append the playback queue.",
                e.what());
        } else {
            FB2K_console_formatter() << "Save Queue load error: " << e.what();
        }
    }
}

// ---------------------------------------------------------------------------
void RunSaveQueue() {
    auto pm = playlist_manager::get();
    if (!pm->queue_is_active()) {
        popup_message::g_show("The playback queue is empty; nothing to save.", "Save Queue");
        return;
    }

    pfc::string8 path;
    if (!uGetOpenFileName(core_api::get_main_window(), kFileDialogFilter,
                          0, kQueueFileExt, "Save playback queue", nullptr, path, TRUE)) {
        return; // user cancelled
    }

    SaveQueueToFilePath(path, true);
}

// ---------------------------------------------------------------------------
void RunLoadQueue(bool p_replace) {
    pfc::string8 path;
    if (!uGetOpenFileName(core_api::get_main_window(), kFileDialogFilter,
                          0, kQueueFileExt,
                          p_replace ? "Load playback queue" : "Append playback queue",
                          nullptr, path, FALSE)) {
        return; // user cancelled
    }

    LoadQueueFromFilePath(path, p_replace, true);
}

namespace {

// Snapshot of the last autosave payload, used to skip no-change writes.
// Queue-change notifications can re-trigger themselves in a loop, and
// rewriting the file on every notification made the playlist view repaint its
// queue badges over and over (visible as flickering queue numbers). An empty
// snapshot means the queue was empty (autosave file removed).
pfc::string8 g_lastAutoSavedBody;
bool g_lastAutoSaveKnown = false;

// Set once the startup restore has been attempted; autosaves are gated on it
// so a quit (or a queue-change notification) before the restore cannot wipe
// the previous session's autosave file just because the live queue is still
// empty. See save_queue.h.
bool g_autoSaveReady = false;

} // namespace

// ---------------------------------------------------------------------------
void MarkAutoSaveReady() { g_autoSaveReady = true; }

bool AutoSaveIsReady() { return g_autoSaveReady; }

// ---------------------------------------------------------------------------
void AutoSaveQueue() {
    if (!g_autoSaveReady) return; // previous session's file must not be touched yet

    pfc::string8 autoPath = core_api::pathInProfile("autosave_queue.fbq2k");
    abort_callback& abort = fb2k::noAbort;

    pfc::string8 body;
    const bool queueActive = build_queue_body(body);

    if (g_lastAutoSaveKnown && body == g_lastAutoSavedBody) return; // nothing changed
    g_lastAutoSavedBody = body;
    g_lastAutoSaveKnown = true;

    try {
        if (!queueActive) {
            if (filesystem::g_exists(autoPath, abort)) {
                try {
                    filesystem::g_remove(autoPath, abort);
                } catch (const std::exception& e) {
                    // Deleting a locked file must not pass silently: the stale
                    // file would resurrect a queue the user thought they
                    // cleared on the next startup.
                    FB2K_console_formatter()
                        << "Save Queue: could not remove the autosave file (" << e.what() << ")";
                }
            }
            return;
        }

        // Acquire a write lock so we cooperate with foobar2000 if the chosen
        // path happens to be a file currently being played.
        auto lock = file_lock_manager::get()->acquire_write(autoPath, abort);

        // rewrite_file goes through a temporary file and an atomic replace, so
        // a crash mid-write cannot leave a truncated queue file behind for the
        // next startup to load.
        filesystem::get(autoPath)->rewrite_file(
            autoPath, abort, 1.0, body.get_ptr(), body.get_length());

        FB2K_console_formatter() << "Save Queue: autosaved the playback queue ("
                                 << file_path_display(autoPath) << ")";
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "Save Queue: autosave failed (" << e.what() << ")";
    } catch (...) {
        FB2K_console_formatter() << "Save Queue: autosave failed (unknown error)";
    }
}

// ---------------------------------------------------------------------------
void AutoLoadQueue() {
    pfc::string8 autoPath = core_api::pathInProfile("autosave_queue.fbq2k");
    abort_callback& abort = fb2k::noAbort;

    try {
        if (filesystem::g_exists(autoPath, abort)) {
            LoadQueueFromFilePath(autoPath, true, false);
        }
    } catch (...) {
        FB2K_console_formatter() << "Save Queue: could not restore the autosaved queue";
    }

    // From here on the live queue reflects reality, so autosaves may treat an
    // empty queue as "user cleared it" again.
    MarkAutoSaveReady();
}

