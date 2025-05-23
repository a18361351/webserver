#ifndef HTTP_CONN_HEADER
#define HTTP_CONN_HEADER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>

#include "locker.h"

class HTTPConn {
public:
    static const int FILENAME_LEN = 260;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    // HTTP方法
    enum METHOD {
        GET = 0, POST, HEAD, PUT, DELETE, 
        TRACE, OPTIONS, CONNECT, PATCH
    };

    // 主状态机状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0, 
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum LINE_STATUS {
        LINE_OK = 0, LINE_OPEN, LINE_BAD
    };
    enum HTTP_CODE {
        NO_REQUEST, GET_REQUEST, BAD_REQUEST, 
        NO_RESOURCE, FILE_REQUEST, FORBIDDEN_REQUEST, 
        INTERNAL_ERROR, SERVICE_UNAVAILABLE, CLOSED_CONNECTION
    };
    enum HTTP_VERSION {
        HTTP1_0 = 0, HTTP1_1, HTTP2_0, HTTP_UNSUPPORTED
    };
    HTTPConn() {}
    ~HTTPConn() {}

    void init(int sockfd, int epollfd, const sockaddr_in& addr);
    void close_conn(bool real_close = true);
    void close_conn_write();
    void process();
    bool read();
    bool write();

    void write_respond(HTTP_CODE code, bool send_and_exit);

private:
    // 初始化连接
    void init();
    // 解析HTTP
    HTTP_CODE process_read();
    // 填充HTTP应答
    bool process_write(HTTP_CODE ret);

    HTTP_CODE parse_requestline(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    // static int m_epollfd;
    int m_epollfd;  // 每个user对应的epollfd可能不同了
    static int m_user_count;

private:
    int m_sockfd{-1};
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];
    int m_end_pos;
    int m_cur_pos;
    int m_start_line;

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    int m_bytes_to_send;
    CHECK_STATE m_check_state;
    METHOD m_method;

    char m_real_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    HTTP_VERSION m_http_ver;
    char* m_host;
    int m_content_length;
    bool m_linger;

    // mmap+writev
    char* m_file_address;
    struct stat m_file_stat;
    // sendfile
    int m_filefd;

    struct iovec m_iv[2];
    int m_iv_count;

    // 已传输数据
    uint m_bytes_sent;
};

#endif