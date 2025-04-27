#include "config.h"
#include <cstring>

// default config
Config::Config() {
    init_default();
}

void Config::init_default() {
    sub_reactors = 1;
    worker_threads = 1;
    use_sendfile = false;

    listen_port = 1234;
    strcpy(this->listen_intf, "0.0.0.0");
}