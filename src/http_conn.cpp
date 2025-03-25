#include "http_conn.h"
#include <cstring>

const char* OK_200_TITLE = "OK";
const char* ERROR_400_TITLE = "Bad Request";
const char* ERROR_400_FORM = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* ERROR_403_TITLE = "Forbidden";
const char* ERROR_403_FORM = "You do not have permission to get file from this server.\n";
const char* ERROR_404_TITLE = "Not Found";
const char* ERROR_404_FORM = "The requested file was not found on this server.\n";
const char* ERROR_500_TITLE = "Internal Server Error";
const char* ERROR_500_FORM = "There was an unusual problem serving the requested file.\n";
/* 网站的根目录 */
const char* DOC_ROOT = "/var/www/html";

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool oneshot) {
    epoll_event e;
    e.data.fd = fd;
    e.events = EPOLLIN | EPOLLET;
    if (oneshot) {
        e.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &e);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    assert(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0) != -1);
    close(fd);
}

void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int HTTPConn::m_user_count = 0;
int HTTPConn::m_epollfd = -1;

void HTTPConn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void HTTPConn::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

void HTTPConn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_conn_closed = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_cur_pos = 0;
    m_end_pos = 0;
    m_write_idx = 0;
    memset(m_read_buf, 0, READ_BUFFER_SIZE);
    memset(m_write_buf, 0, WRITE_BUFFER_SIZE);
    memset(m_real_file, 0, FILENAME_LEN);
}

bool HTTPConn::read() {
        if (m_end_pos >= READ_BUFFER_SIZE) {
            return false;
        }
    
        int bytes_read = 0;
        bool connection_closed = false;
    
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_end_pos, READ_BUFFER_SIZE - m_end_pos, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // 正常结束循环
                }
                return false;   // error
            } else if (bytes_read == 0) {
                connection_closed = true;
                break;
            }
            m_end_pos += bytes_read;
        }
    
        // 添加成员变量标记连接已关闭
        m_conn_closed = connection_closed;
    
        // 只要无错误发生，即使客户端关闭也返回true
        return true;
    
}


HTTPConn::LINE_STATUS HTTPConn::parse_line() {
    char temp;
    // m_cur_pos指向buffer中当前正在分析的字节，m_end_pos为buffer中尾部的下一字节
    while (m_cur_pos < m_end_pos) {
        temp = m_read_buf[m_cur_pos];
        if (temp == '\r') {
            if (m_cur_pos + 1 == m_end_pos) {
                // 若\r为最后一个被读入的数据，则表示本次分析没有读取到一个完整的行，返回LINE_OPEN
                return LINE_OPEN;
            } else if (m_read_buf[m_cur_pos + 1] == '\n') {
                // 读取到了\r\n，返回LINE_OK，此时m_cur_pos应该指向新行的开头
                m_read_buf[m_cur_pos++] = 0;  // \r
                m_read_buf[m_cur_pos++] = 0;  // \n
                return LINE_OK;
            } else {
                // \r之后没有\n
                return LINE_BAD;
            }
        } else if (temp == '\n') {
            // \r\n为一个完整的换行
            if ((m_cur_pos > 1) && (m_read_buf[m_cur_pos - 1] == '\r')) {
                m_read_buf[m_cur_pos - 1] = 0;    // \r
                m_read_buf[m_cur_pos++] = 0;      // \n
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        }
        m_cur_pos++;
    }
    // 其他情况要求继续解析，返回LINE_OPEN，此时m_cur_pos应该指向m_end_pos
    return LINE_OPEN;
}

HTTPConn::HTTP_CODE HTTPConn::parse_requestline(char* text) {
    // strpbrk在temp中寻找pattern中任一字符首次出现的位置
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        // 要求请求行中必须有空白字符或'\t'字符
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        // GET
        m_method = GET;
    // } else if (strcasecmp(method, "POST") == 0) {
    //     // POST
    //     m_method = POST;
    } else {
        // 暂时不支持其他的方法/或者客户端传入的非法的请求
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    // 版本号检查
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        // 版本不匹配
        return BAD_REQUEST;
    }

    // 检查URL是否合法
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;    // 状态从请求行分析转移至头部字段分析
    return NO_REQUEST;
}

HTTPConn::HTTP_CODE HTTPConn::parse_headers(char* text) {
    // 遇到一个空行，说明头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需读取m_content_length字节的消息体，状态机转移至CHECK_STATE_CONTENT
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5; 
        text += strspn(text, " \t");
        m_host = text;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 连接方式
        text += 11;
        text += strspn(text, "\t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else {
        // Unknown header
    }
    return NO_REQUEST;
}

HTTPConn::HTTP_CODE HTTPConn::parse_content(char* text) {
    // 判断消息是否被完整读入了
    if (m_end_pos >= (m_content_length + m_cur_pos)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机
HTTPConn::HTTP_CODE HTTPConn::process_read() {
    LINE_STATUS linestatus = LINE_OK;
    HTTP_CODE retcode = NO_REQUEST;
    char* text = 0;
    // 主状态机
    while (((m_check_state == CHECK_STATE_CONTENT) && (linestatus == LINE_OK))
         || ((linestatus = parse_line()) == LINE_OK)) {
        text = get_line();   // start_line是行在buffer中的起始位置
        m_start_line = m_cur_pos;
        // 根据当前主状态机状态进行操作
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE:   // 请求行
                retcode = parse_requestline(text);
                if (retcode == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            case CHECK_STATE_HEADER:    // 头部
                retcode = parse_headers(text);
                if (retcode == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (retcode == GET_REQUEST) {
                    return do_request();
                }
                break;
            case CHECK_STATE_CONTENT: // 内容
                retcode = parse_content(text);
                if (retcode == GET_REQUEST) {
                    return do_request();
                }
                linestatus = LINE_OPEN;
                break;
            default:    // 状态机进入非法状态
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;  // 仍需要继续读取数据
}

// 得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性
// 目标文件存在且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
HTTPConn::HTTP_CODE HTTPConn::do_request() {
    // 初始化路径
    strncpy(m_real_file, DOC_ROOT, FILENAME_LEN);
    m_real_file[FILENAME_LEN - 1] = '\0'; // 确保终止
    int base_len = strlen(DOC_ROOT);

    // 处理根路径或拼接URL
    if (strcmp(m_url, "/") == 0) {
        if (base_len + 11 >= FILENAME_LEN) { // "/index.html" + \0
            return BAD_REQUEST;
        }
        strncat(m_real_file, "/index.html", FILENAME_LEN - base_len - 1);
    } else {
        // 检查URL合法性（防止路径遍历）
        if (strstr(m_url, "..")) {
            return FORBIDDEN_REQUEST;
        }
        if (base_len + strlen(m_url) + 1 > FILENAME_LEN) {
            return BAD_REQUEST;
        }
        strncat(m_real_file, m_url, FILENAME_LEN - base_len - 1);
    }

    // 解析规范路径并检查是否在DOC_ROOT下
    char resolved_path[FILENAME_LEN];
    if (realpath(m_real_file, resolved_path) == NULL) {
        return NO_RESOURCE;
    }
    if (strncmp(resolved_path, DOC_ROOT, strlen(DOC_ROOT)) != 0) {
        return FORBIDDEN_REQUEST;
    }
    strcpy(m_real_file, resolved_path);
    
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (access(m_real_file, R_OK) != 0) {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

bool HTTPConn::write() {
    int temp = 0;
    int bytes_sent = 0;
    int bytes_remain = m_write_idx;
    if (bytes_remain == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_sent += temp;
        bytes_remain -= temp;

        if (bytes_remain <= 0) {
            // 根据Connection字段决定是否立即关闭连接
            unmap();
            if (m_linger) {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            } else {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

bool HTTPConn::add_response(const char* fmt, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, fmt);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, fmt, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool HTTPConn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool HTTPConn::add_headers(int content_len) {
    bool ret = true;
    ret = add_content_length(content_len);
    ret = ret && add_linger();
    ret = ret && add_blank_line();
    return ret;
}
bool HTTPConn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}
bool HTTPConn::add_linger() {
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}
bool HTTPConn::add_blank_line() {
    return add_response("\r\n");
}
bool HTTPConn::add_content(const char* content) {
    return add_response("%s", content);
}
bool HTTPConn::process_write(HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, ERROR_500_TITLE);
            add_headers(strlen(ERROR_500_FORM));
            if (!add_content(ERROR_500_FORM)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, ERROR_400_TITLE);
            add_headers(strlen(ERROR_400_FORM));
            if (!add_content(ERROR_400_FORM)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, ERROR_404_TITLE);
            add_headers(strlen(ERROR_404_FORM));
            if (!add_content(ERROR_404_FORM)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, ERROR_403_TITLE);
            add_headers(strlen(ERROR_403_FORM));
            if (!add_content(ERROR_403_FORM)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, OK_200_TITLE);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            } else {
                const char* OK_STR = "<html><body></body></html>";
                add_headers(strlen(OK_STR));
                if (!add_content(OK_STR)) {
                    return false;
                }
            }
            break;
        }
        default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void HTTPConn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    if (m_conn_closed) {    // 对方已经关闭连接
        close_conn(true);   
    } else {
        modfd(m_epollfd, m_sockfd, EPOLLOUT);
    }
}

void HTTPConn::unmap(){
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
