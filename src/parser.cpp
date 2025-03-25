// HTTP Request Parser State-machine
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define BUF_SIZE 4096
#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DPRINT(fmt, ...) \
    do {\
    printf("%s:%s():%d>" fmt "\n", \
    (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__), \
    __func__, \
    __LINE__, \
    ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINT(fmt, ...) ((void)0)
#endif

static const char* resps[] = {
    "OK",       // No problem
    "Error",    // Any error occured
};

/**
 * HTTP请求：
 *  由请求行（METHOD http://url.com HTTP/1.0）和
 * 请求头部组成
 */

enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER
};
enum LINE_STATUS {
    LINE_OK = 0, LINE_OPEN, LINE_BAD
};
enum HTTP_CODE {
    NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
};

LINE_STATUS parse_line(char* buffer, int& cur_pos, int& end_pos) {
    char temp;
    // cur_pos指向buffer中当前正在分析的字节，end_pos为buffer中尾部的下一字节
    while (cur_pos < end_pos) {
        temp = buffer[cur_pos];
        if (temp == '\r') {
            if (cur_pos + 1 == end_pos) {
                // 若\r为最后一个被读入的数据，则表示本次分析没有读取到一个完整的行，返回LINE_OPEN
                DPRINT("\\r is last character");
                return LINE_OPEN;
            } else if (buffer[cur_pos + 1] == '\n') {
                // 读取到了\r\n，返回LINE_OK，此时cur_pos应该指向新行的开头
                DPRINT("line ok");
                buffer[cur_pos++] = 0;  // \r
                buffer[cur_pos++] = 0;  // \n
                return LINE_OK;
            } else {
                DPRINT("bad line");
                return LINE_BAD;
            }
        } else if (temp == '\n') {
            // \r\n为一个完整的换行
            if ((cur_pos > 1) && (buffer[cur_pos - 1] == '\r')) {
                DPRINT("line ok");
                buffer[cur_pos - 1] = 0;    // \r
                buffer[cur_pos++] = 0;      // \n
                return LINE_OK;
            } else {
                DPRINT("bad line");
                return LINE_BAD;
            }
        } else {
            // 继续分析
        }
        cur_pos++;
    }
    // 其他情况要求继续解析，返回LINE_OPEN，此时cur_pos应该指向end_pos
    return LINE_OPEN;
}

HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkstate) {
    // strpbrk在temp中寻找pattern中任一字符首次出现的位置
    char* url = strpbrk(temp, " \t");
    if (!url) {
        // 要求请求行中必须有空白字符或'\t'字符
        return BAD_REQUEST;
    }
    *url++ = '\0';

    char* method = temp;
    if (strcasecmp(method, "GET") == 0) {
        // GET
        DPRINT("GET method");
    } else if (strcasecmp(method, "POST") == 0) {
        // POST
        DPRINT("POST method");
    } else {
        // 暂时不支持其他的方法/或者客户端传入的非法的请求
        DPRINT("invaild method %s", method);
        return BAD_REQUEST;
    }

    url += strspn(url, " \t");
    // 版本号检查
    char* version = strpbrk(url, " \t");
    if (!version) {
        DPRINT("Version string missing");
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0) {
        DPRINT("HTTP protocol version not match");
        return BAD_REQUEST;
    }

    // 检查URL是否合法
    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;
        url = strchr(url, '/');
    }
    if (!url || url[0] != '/') {
        DPRINT("Invalid URL");
        return BAD_REQUEST;
    }
    DPRINT("URL is %s", url);
    checkstate = CHECK_STATE_HEADER;    // 状态从请求行分析转移至头部字段分析
    return NO_REQUEST;
}

HTTP_CODE parse_headers(char* temp) {
    // 遇到一个空行，说明得到了一个正确的HTTP请求
    if (temp[0] == '\0') {
        return GET_REQUEST;
    } else if (strncasecmp(temp, "Host:", 5) == 0) {
        temp += 5; temp += strspn(temp, " \t");
        DPRINT("Request host is %s", temp);
    } else {
        // 其他字段
        DPRINT("Other header");
    }
    return NO_REQUEST;
}

HTTP_CODE parse_content(char* buffer, int& cur_pos, CHECK_STATE& checkstate, int& end_pos, int& start_line) {
    LINE_STATUS linestatus = LINE_OK;
    HTTP_CODE retcode = NO_REQUEST;
    // 主状态机
    while ((linestatus = parse_line(buffer, cur_pos, end_pos)) == LINE_OK) {
        char* temp = buffer + start_line;   // start_line是行在buffer中的起始位置
        start_line = cur_pos;
        // 根据当前主状态机状态进行操作
        switch (checkstate) {
            case CHECK_STATE_REQUESTLINE:   // 请求行
                retcode = parse_requestline(temp, checkstate);
                if (retcode == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            case CHECK_STATE_HEADER:    // 头部
                retcode = parse_headers(temp);
                if (retcode == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (retcode == GET_REQUEST) {
                    return GET_REQUEST;
                }
                break;
            default:    // 状态机进入非法状态
                return INTERNAL_ERROR;
        }
    }
    if (linestatus == LINE_OPEN) {
        return NO_REQUEST;  // 仍需要继续读取数据
    } else {
        return BAD_REQUEST;
    }
}

// int main() {
//     const char* ip_addr = "0.0.0.0";
//     int port = 80;
//     // 地址填充
//     sockaddr_in sckAddr;
//     bzero(&sckAddr, sizeof(sckAddr));
//     sckAddr.sin_family = AF_INET; // 地址族
//     inet_pton(AF_INET, ip_addr, &sckAddr.sin_addr); // 地址
//     sckAddr.sin_port = htons(port); // 端口，转为网络字节序

//     int ret = 0;
//     int listener = socket(PF_INET, SOCK_STREAM, 0);
//     assert(listener >= 0);

//     ret = bind(listener, (struct sockaddr*)&sckAddr, sizeof(sckAddr));
//     assert(ret == 0);

//     ret = listen(listener, 5);
//     assert(ret == 0);

//     struct sockaddr_in client_address;
//     socklen_t client_addrlen = sizeof(client_address);
//     int fd = accept(listener, (struct sockaddr*)&client_address, &client_addrlen);
//     if (fd < 0) {
//         DPRINT("Error occured in accept(), errno = %d", errno);
//     } else {
//         char buffer[BUF_SIZE];
//         memset(buffer, 0, BUF_SIZE);
//         int data_read = 0;
//         int cur_pos = 0;
//         int end_pos = 0;
//         int start_line = 0;
//         CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
//         while (1) {
//             data_read = recv(fd, buffer + cur_pos, BUF_SIZE - cur_pos, 0);
//             DPRINT("Data received len %d", data_read);
//             if (data_read == -1) {
//                 DPRINT("recv failed");
//                 break;
//             } else if (data_read == 0) {
//                 printf("Remote connection closed\n");
//                 break;
//             }
//             end_pos += data_read;
//             HTTP_CODE result = parse_content(buffer, cur_pos, checkstate, end_pos, start_line);
//             if (result == NO_REQUEST) {
//                 continue;
//             } else if (result == GET_REQUEST) {
//                 send(fd, resps[0], strlen(resps[0]), 0);
//                 break;
//             } else {
//                 send(fd, resps[1], strlen(resps[1]), 0);
//                 break;
//             }
//         }
//         close(fd);
//     }
//     close(listener);
//     return 0;
// }