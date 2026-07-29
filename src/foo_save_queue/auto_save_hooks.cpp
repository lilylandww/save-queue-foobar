#include "stdafx.h"
#include "save_queue.h"

namespace {

class auto_save_initquit : public initquit {
public:
    void on_init() override {
        AutoLoadQueue();
    }

    void on_quit() override {
        AutoSaveQueue();
    }
};

FB2K_SERVICE_FACTORY(auto_save_initquit);

} // namespace
