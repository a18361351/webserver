#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include <iostream>
#include <string.h>

#define CONFIG_MEMBERS \
    X(int,    sub_reactors)       \
    X(int,    worker_threads)     \
    X(bool,   use_sendfile)       \
    X(int,    listen_port)        \
    X_ARRAY(char,   listen_intf, 80)

struct Config {
    #define X(type, name) type name;
    #define X_ARRAY(type, name, size) type name[size];
    CONFIG_MEMBERS
    #undef X
    #undef X_ARRAY

    void print() const {
        std::cout << "Configure:\n";
        // 打印普通成员
        #define X(type, name) \
            std::cout << #name << ": " << name << '\n';
        // 打印数组成员，直接输出名称和内容
        #define X_ARRAY(type, name, size) \
            std::cout << #name << ": \"" << name << "\"\n";
        CONFIG_MEMBERS
        #undef X
        #undef X_ARRAY
    }

    Config();
    void load_from(const char* config_path);
    void init_default();
};

#endif