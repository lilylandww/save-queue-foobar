#pragma once

// Entry points invoked from the File > Save Queue menu commands.
// Implemented in save_queue.cpp; wired up in mainmenu.cpp.

// Write the current playback queue to a user-chosen file.
void RunSaveQueue();

// Read a previously saved queue from a file. When p_replace is true the current
// queue is flushed first; otherwise the loaded items are appended.
void RunLoadQueue(bool p_replace);

// Save/load queue to/from explicit file path, optionally showing popups.
void SaveQueueToFilePath(const char* path, bool show_popups);
void LoadQueueFromFilePath(const char* path, bool p_replace, bool show_popups);

// Lifecycle hooks invoked on foobar2000 startup and shutdown, and whenever the
// live queue changes (implemented in save_queue.cpp; wired up in
// auto_save_hooks.cpp). Together they keep <profile>/autosave_queue.fbq2k in
// sync with the queue so it survives restarts - including abnormal ones.
void AutoSaveQueue();
void AutoLoadQueue();

// Autosaves are gated on the startup restore having been attempted: before
// that, the live queue is empty merely because nothing was restored yet, and
// treating that as "user cleared the queue" would delete the file holding the
// previous session's queue. Set by auto_save_hooks.cpp once the restore ran
// (or found nothing to restore).
void MarkAutoSaveReady();
bool AutoSaveIsReady();
