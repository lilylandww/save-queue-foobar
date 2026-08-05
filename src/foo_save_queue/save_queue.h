#pragma once

// Entry points invoked from the File > Save Queue menu commands.
// Implemented in save_queue.cpp; wired up in mainmenu.cpp.

// Write the current playback queue to a user-chosen file.
void RunSaveQueue();

// Read a previously saved queue from a file. When p_replace is true the current
// queue is flushed first; otherwise the loaded items are appended.
void RunLoadQueue(bool p_replace);
