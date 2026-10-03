#include "stdafx.h"
#include "save_queue.h"

#include <SDK/initquit.h>
#include <SDK/threadsLite.h>

#include <atomic>

namespace {

std::atomic<bool> g_shuttingDown{false};

// Queues the startup restore. Runs from init_stages::after_ui_init - the
// documented point at which the UI is initialized - and is pushed through the
// main-thread callback queue so it executes outside the init-stage dispatch
// stack. Restoring before the playlist view is around to observe the queue
// changes leaves the restored items without their queue badges.
void runRestore() {
    fb2k::inMainThread([] {
        if (!g_shuttingDown) AutoLoadQueue();
    });
}

FB2K_ON_INIT_STAGE(runRestore, init_stages::after_ui_init);

// Restores the auto-saved queue at startup and writes it back on a clean exit.
class auto_save_initquit : public initquit {
public:
    void on_quit() override {
        g_shuttingDown = true;
        // If the restore never ran (very short session), the live queue is
        // empty merely because nothing was restored yet; AutoSaveQueue's
        // ready-gate makes it a no-op instead of deleting the previous
        // session's file.
        AutoSaveQueue();
    }
};

// Keeps <profile>/autosave_queue.fbq2k in sync with the live queue so an
// abrupt termination (crash, process kill, power loss) still leaves a current
// queue file behind for the next startup.
class queue_change_autosaver : public playback_queue_callback {
public:
    void on_changed(t_change_origin) override {
        // The write must NOT happen inside on_changed: that is called from
        // inside the core's queue-change dispatch, where re-entering playlist
        // APIs and doing file I/O visibly slowed startup. Queue the work onto
        // the main thread instead (off the notification stack), and collapse
        // bursts - a restore or multi-item edit fires on_changed per item -
        // into a single write via the pending flag.
        if (g_shuttingDown) return;
        if (m_pending.exchange(true)) return;
        fb2k::inMainThread([this] {
            m_pending = false;
            AutoSaveQueue();
        });
    }

private:
    std::atomic<bool> m_pending{false};
};

FB2K_SERVICE_FACTORY(auto_save_initquit);
FB2K_SERVICE_FACTORY(queue_change_autosaver);

} // namespace
