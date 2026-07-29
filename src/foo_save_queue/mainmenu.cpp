#include "stdafx.h"
#include "save_queue.h"

// ---------------------------------------------------------------------------
// These GUIDs are unique to foo_save_queue (generated with uuidgen). When
// reusing this code in another component, generate fresh ones -- duplicate
// GUIDs cause foobar2000 to refuse to load the component.
// ---------------------------------------------------------------------------
static const GUID guid_group =
    { 0x72bc2dd8, 0xdf17, 0x4a4f, { 0xbf, 0xd1, 0x50, 0xce, 0xd6, 0x00, 0x23, 0x9d } };
static const GUID guid_cmd_save =
    { 0x85fd04e5, 0x3b6f, 0x4f07, { 0x8a, 0x6a, 0x68, 0xfd, 0xda, 0x00, 0x87, 0x68 } };
static const GUID guid_cmd_load_replace =
    { 0xf66e4d06, 0x843a, 0x4bb7, { 0xbe, 0xbf, 0x87, 0xf4, 0xb7, 0xf1, 0x3e, 0x76 } };
static const GUID guid_cmd_append =
    { 0x5d0d1e5a, 0x73ba, 0x4048, { 0x81, 0x87, 0x46, 0x3b, 0xbe, 0x87, 0xc4, 0x3a } };

// Popup group "Save Queue" nested under the top-level File menu.
static mainmenu_group_popup_factory g_mainmenu_group(
    guid_group, mainmenu_groups::file, mainmenu_commands::sort_priority_dontcare, "Save Queue");

class mainmenu_commands_save_queue : public mainmenu_commands {
public:
    enum {
        cmd_save = 0,
        cmd_load_replace,
        cmd_append,
        cmd_total
    };

    t_uint32 get_command_count() override { return cmd_total; }

    GUID get_command(t_uint32 p_index) override {
        switch (p_index) {
            case cmd_save:         return guid_cmd_save;
            case cmd_load_replace: return guid_cmd_load_replace;
            case cmd_append:       return guid_cmd_append;
            default:               uBugCheck(); // unreachable with valid index
        }
    }

    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        switch (p_index) {
            case cmd_save:         p_out = "Save playback queue..."; break;
            case cmd_load_replace: p_out = "Load playback queue (replace)..."; break;
            case cmd_append:       p_out = "Append playback queue..."; break;
            default:               uBugCheck();
        }
    }

    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        switch (p_index) {
            case cmd_save:
                p_out = "Save the current playback queue to a file (.fbq2k)."; return true;
            case cmd_load_replace:
                p_out = "Clear the queue and restore a previously saved playback queue."; return true;
            case cmd_append:
                p_out = "Append a previously saved playback queue to the current queue."; return true;
            default: return false;
        }
    }

    GUID get_parent() override { return guid_group; }

    void execute(t_uint32 p_index, service_ptr_t<service_base>) override {
        switch (p_index) {
            case cmd_save:         RunSaveQueue();        break;
            case cmd_load_replace: RunLoadQueue(true);    break;
            case cmd_append:       RunLoadQueue(false);   break;
            default:               uBugCheck();
        }
    }
};

// Register the commands with foobar2000.
static mainmenu_commands_factory_t<mainmenu_commands_save_queue> g_mainmenu_commands_factory;
