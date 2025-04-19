#include <openssl/ssl.h>
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
#define MAX_EVENT_NUMBER 10000

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

    ThreadPool<HTTPConn>* pool = nullptr;
    try {
        pool = new ThreadPool<HTTPConn>(8);
    } catch (...) {
        DPRINT("Unable to init thread pool.");
        return 1;
    }

    // 为每个可能的客户都预先分配一个HTTPConn对象
    HTTPConn* users = new HTTPConn[MAX_FD];
    assert(users);
    // int user_count = 0;

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

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(65535);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    HTTPConn::m_epollfd = epollfd;

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
                    users[connfd].init(connfd, cli_addr);
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
                // RDHUP/HUP事件，为远方关闭连接
                DPRINT("[%d]RDHUP/HUP event, closing connection", sockfd);
                users[sockfd].close_conn();
            } else if (events[i].events & (EPOLLERR)) {
                // 异常时直接关闭客户连接
                DPRINT("[%d]Error: closing connection, event = %u", sockfd, events[i].events);
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    if (!pool->append(users + sockfd)) {
                        // 队列已满
                        // 应该返回503
                        users[sockfd].write_respond(HTTPConn::SERVICE_UNAVAILABLE, true);
                    }
                } else {
                    DPRINT("[%d]Read error: closing connection", sockfd);
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    DPRINT("[%d]Write done: closing connection", sockfd);
                    // users[sockfd].close_conn();
                    users[sockfd].close_conn_write();
                }
            } else {
                // do nothing
            }
        }
    }
    DPRINT("Cleanup");
    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;
    return 0;
}