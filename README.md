# Webserver
简易的webserver，使用主从Reactor模式+epoll ET模式+线程池提高效率

# 系统环境
- Ubuntu 22.04（或其他Linux系统）
- GCC 11.4.0
- C++11

# 快速使用
编译：
```sh
make
```
运行：
```sh
# 指定端口，默认1234
bin/server [port]
# 或者指定ip+端口
bin/server local_ip port
```

# 参考
《Linux高性能服务器编程》，游双著

[qinguoyi/TinyWebServer:🔥 Linux下C++轻量级WebServer服务器](https://github.com/qinguoyi/TinyWebServer)