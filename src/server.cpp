#include <exception>
#include <openssl/ssl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "thread_pool.h"
#include "http_conn.h"

// #define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DPRINT(fmt, ...) \
    do {\
    printf("%s:%d %s()>" fmt "\n", \
    (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__), \
    __LINE__, \
    __func__, \
    ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINT(fmt, ...) ((void)0)
#endif


#define MAX_FD 65536
#define MAX_EVENT_NUMBER 1024

constexpr int sub_reactors = 1;
constexpr int worker_threads = 1;

extern void addfd(int epollfd, int fd, bool oneshot, int trig_mode = 1);
extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info) {
    DPRINT("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}


struct Context {
    ThreadPool<HTTPConn>* pool;
    HTTPConn* users;
    int epollfd;
    int listener;
    int sub_reactors_epollfd[sub_reactors];
};

// main reactor
// 主反应堆负责监听listenfd，并负责将接受的连接分发给sub reactor
void* main_reactor([[maybe_unused]]void* arg) {
    Context ctx = *(Context*)arg;
    int epollfd = ctx.epollfd;
    int listenfd = ctx.listener;
    HTTPConn* users = ctx.users;
    epoll_event events[MAX_EVENT_NUMBER];
    int rr_counter = 0; // round robin
    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            DPRINT("epoll failure");
            break;
        }
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                while (true) {
                    struct sockaddr_in cli_addr;
                    socklen_t cli_addr_len = sizeof(cli_addr);
                    int connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &cli_addr_len);
                    if (connfd < 0) {
                        if (errno == EAGAIN) {
                            break;  // All fds get!
                        }
                        perror("Error in accept()");
                        continue;
                    }
                    if (HTTPConn::m_user_count >= MAX_FD) {
                        show_error(connfd, "Internal server busy");
                        continue;
                    }
                    DPRINT("[%d]New connection incoming", connfd);
                    // users[connfd].init(connfd, sub_epollfd, cli_addr);
                    users[connfd].init(connfd, ctx.sub_reactors_epollfd[rr_counter], cli_addr);
                    DPRINT("Dispatch connection fd = %d -> subreactor epollfd = %d", connfd, ctx.sub_reactors_epollfd[rr_counter]);
                    rr_counter = (rr_counter + 1) % sub_reactors;
                }
            } else {
                // 按理来说表中只可能有一个sockfd
                throw std::logic_error("Main reactor should only have listenfd");
            }
        }
    }
}

// sub reactor
// 从反应堆监听从主反应堆中传入的fd，从反应堆的epollfd直接在参数中传入
void* sub_reactor(void* arg) {
    Context ctx = *(Context*)arg;
    int epollfd = ctx.epollfd;
    DPRINT("sub reactor's epollfd = %d", epollfd);
    HTTPConn* users = ctx.users;
    ThreadPool<HTTPConn>* pool = ctx.pool;
    epoll_event events[MAX_EVENT_NUMBER];
    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            DPRINT("epoll failure");
            break;
        }
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
                // RDHUP/HUP事件，为远方关闭连接
                DPRINT("[%d.%d]RDHUP/HUP event, closing connection", epollfd, sockfd);
                users[sockfd].close_conn();
            } else if (events[i].events & (EPOLLERR)) {
                // 异常时直接关闭客户连接
                DPRINT("[%d.%d]Error: closing connection, event = %u", epollfd, sockfd, events[i].events);
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    if (!pool->append(users + sockfd)) {
                        // 队列已满
                        // 应该返回503
                        users[sockfd].write_respond(HTTPConn::SERVICE_UNAVAILABLE, true);
                        DPRINT("[%d.%d]Queue is full", epollfd, sockfd);
                    }
                } else {
                    DPRINT("[%d.%d]Read error: closing connection", epollfd, sockfd);
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    DPRINT("[%d.%d]Write done: closing connection", epollfd, sockfd);
                    // users[sockfd].close_conn();
                    users[sockfd].close_conn_write();
                }
            } else {
                // do nothing
            }
        }
    }
}

int main(int argc, char* argv[]) {
    char ip[80];
    int port = atoi(argv[2]);

    // Cmd parse
    if (argc == 1) {
        strncpy(ip, "0.0.0.0", 80);
        port = 1234;
    } else if (argc == 2) {
        strncpy(ip, "0.0.0.0", 80);
        port = atoi(argv[1]);
    } else if (argc == 3) {
        strncpy(ip, argv[1], 80);
        port = atoi(argv[2]);
    } else {
        printf("usage:\t%s [port]\n\t%s local_ip port\n", argv[0], argv[0]);
        return -1;
    }

    addsig(SIGPIPE, SIG_IGN);

    
    // Context上下文类型创建
    Context ctx;
    
    // 线程池创建
    try {
        ctx.pool = new ThreadPool<HTTPConn>(worker_threads);
    } catch (...) {
        DPRINT("Unable to init thread pool.");
        exit(-1);
    }

    // 为每个可能的客户都预先分配一个HTTPConn对象
    // HTTPConn* users = new HTTPConn[MAX_FD];
    ctx.users = new HTTPConn[MAX_FD];
    assert(ctx.users);
    // int user_count = 0;

    // listener初始化+监听
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {1, 0};
    int reuse = 1;
    assert(setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp)) >= 0);
    assert(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if (ret < 0) {
        perror("Unable to bind port");
        close(listenfd);
        return -1;
    }

    ret = listen(listenfd, 100);
    assert(ret >= 0);
    
    ctx.listener = listenfd;

    // sub reactors初始化
    int sub_epollfds[sub_reactors] = {-1};
    pthread_t sub_reactor_threads[sub_reactors];
    Context sub_ctx[sub_reactors];
    for (int i = 0; i < sub_reactors; i++) {
        sub_epollfds[i] = epoll_create(65535);  // size parameter is unused!
        // sub reactor上下文初始化
        sub_ctx[i] = ctx;
        sub_ctx[i].epollfd = sub_epollfds[i];
        sub_ctx[i].listener = -1;
        int ret = pthread_create(sub_reactor_threads + i, NULL, sub_reactor, sub_ctx + i);
        if (ret != 0) {
            // error when create thread
            perror("Unable to start new thread");
            exit(-1);
        }
        printf("create sub-reactor thread %d\n", i);
        pthread_detach(sub_reactor_threads[i]);
    }


    // 主线程epollfd创建
    int epollfd = epoll_create(65535);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    ctx.epollfd = epollfd;
    for (int i = 0; i < sub_reactors; i++) {
        ctx.sub_reactors_epollfd[i] = sub_epollfds[i];
    }

    main_reactor(&ctx);

    DPRINT("Cleanup");
    // TODO: 更完善的Cleanup
    close(epollfd);
    close(listenfd);
    delete[] ctx.users;
    delete ctx.pool;
    return 0;
}