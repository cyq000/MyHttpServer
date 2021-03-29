#include "httpconnection.hpp"
#include "log.hpp"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

#define connfdET //边缘触发非阻塞
//#define connfdLT //水平触发阻塞

#define listenfdET //边缘触发非阻塞
//#define listenfdLT //水平触发阻塞

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//网站根目录
const char *doc_root = "/home/cyq/projects/httpServer/date";

//将表中的用户名和密码放入map
map<string, string> _users;
locker _lock;

void http_conn::initmysql_result(Mysql_conection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        _users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::_user_count = 0;
int http_conn::_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (_sockfd != -1))
    {
        removefd(_epollfd, _sockfd);
        _sockfd = -1;
        _user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    _sockfd = sockfd;
    _address = addr;
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(_epollfd, sockfd, true);
    _user_count++;
    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    _mysql = NULL;
    _bytes_to_send = 0;
    _bytes_have_send = 0;
    _check_state = CHECK_STATE_REQUESTLINE;
    _linger = false;
    _method = GET;
    _url = 0;
    _version = 0;
    _content_length = 0;
    _host = 0;
    _start_line = 0;
    _checked_idx = 0;
    _read_idx = 0;
    _write_idx = 0;
    _cgi = 0;
    memset(_read_buf, '\0', READ_BUFFER_SIZE);
    memset(_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; _checked_idx < _read_idx; ++_checked_idx)
    {
        temp = _read_buf[_checked_idx];
        if (temp == '\r')
        {
            if ((_checked_idx + 1) == _read_idx)
                return LINE_OPEN;
            else if (_read_buf[_checked_idx + 1] == '\n')
            {
                _read_buf[_checked_idx++] = '\0';
                _read_buf[_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (_checked_idx > 1 && _read_buf[_checked_idx - 1] == '\r')
            {
                _read_buf[_checked_idx - 1] = '\0';
                _read_buf[_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

#ifdef connfdLT

    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0)
    {
        return false;
    }

    return true;

#endif

#ifdef connfdET
    while (true)
    {
        bytes_read = recv(_sockfd, _read_buf + _read_idx, READ_BUFFER_SIZE - _read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        _read_idx += bytes_read;
    }
    return true;
#endif
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    _url = strpbrk(text, " \t");
    if (!_url)
    {
        return BAD_REQUEST;
    }
    *_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        _method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        _method = POST;
        _cgi = 1;
    }
    else
        return BAD_REQUEST;
    _url += strspn(_url, " \t");
    _version = strpbrk(_url, " \t");
    if (!_version)
        return BAD_REQUEST;
    *_version++ = '\0';
    _version += strspn(_version, " \t");
    if (strcasecmp(_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(_url, "http://", 7) == 0)
    {
        _url += 7;
        _url = strchr(_url, '/');
    }

    if (strncasecmp(_url, "https://", 8) == 0)
    {
        _url += 8;
        _url = strchr(_url, '/');
    }

    if (!_url || _url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(_url) == 1)
        strcat(_url, "judge.html");
    _check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (_content_length != 0)
        {
            _check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            _linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        _content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        _host = text;
    }
    else
    {
        //printf("oop!unknow header: %s\n",text);
        LOG_INFO("oop!unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (_read_idx >= (_content_length + _checked_idx))
    {
        text[_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        _string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ((_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        _start_line = _checked_idx;
        LOG_INFO("%s", text);
        Log::get_instance()->flush();
        switch (_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(_url, '/');

    //处理cgi
    if (_cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = _url[1];

        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/");
        strcat(_url_real, _url + 2);
        strncpy(_real_file + len, _url_real, FILENAME_LEN - len - 1);
        free(_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; _string[i] != '&'; ++i)
            name[i - 5] = _string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; _string[i] != '\0'; ++i, ++j)
            password[j] = _string[i];
        password[j] = '\0';

        //同步线程登录校验
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (_users.find(name) == _users.end())
            {

                _lock.lock();
                int res = mysql_query(_mysql, sql_insert);
                _users.insert(pair<string, string>(name, password));
                _lock.unlock();

                if (!res)
                    strcpy(_url, "/log.html");
                else
                    strcpy(_url, "/registerError.html");
            }
            else
                strcpy(_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (_users.find(name) != _users.end() && _users[name] == password)
                strcpy(_url, "/welcome.html");
            else
                strcpy(_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0')
    {
        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/register.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/log.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/picture.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/video.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(_url_real, "/fans.html");
        strncpy(_real_file + len, _url_real, strlen(_url_real));

        free(_url_real);
    }
    else
        strncpy(_real_file + len, _url, FILENAME_LEN - len - 1);

    if (stat(_real_file, &_file_stat) < 0)
        return NO_RESOURCE;
    if (!(_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(_file_stat.st_mode))
        return BAD_REQUEST;
    int fd = open(_real_file, O_RDONLY);
    _file_address = (char *)mmap(0, _file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (_file_address)
    {
        munmap(_file_address, _file_stat.st_size);
        _file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;

    if (_bytes_to_send == 0)
    {
        modfd(_epollfd, _sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(_sockfd, _iv, _iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(_epollfd, _sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        _bytes_have_send += temp;
        _bytes_to_send -= temp;
        if (_bytes_have_send >= _iv[0].iov_len)
        {
            _iv[0].iov_len = 0;
            _iv[1].iov_base = _file_address + (_bytes_have_send - _write_idx);
            _iv[1].iov_len = _bytes_to_send;
        }
        else
        {
            _iv[0].iov_base = _write_buf + _bytes_have_send;
            _iv[0].iov_len = _iv[0].iov_len - _bytes_have_send;
        }

        if (_bytes_to_send <= 0)
        {
            unmap();
            modfd(_epollfd, _sockfd, EPOLLIN);

            if (_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if (_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(_write_buf + _write_idx, WRITE_BUFFER_SIZE - 1 - _write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - _write_idx))
    {
        va_end(arg_list);
        return false;
    }
    _write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", _write_buf);
    Log::get_instance()->flush();
    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (_file_stat.st_size != 0)
        {
            add_headers(_file_stat.st_size);
            _iv[0].iov_base = _write_buf;
            _iv[0].iov_len = _write_idx;
            _iv[1].iov_base = _file_address;
            _iv[1].iov_len = _file_stat.st_size;
            _iv_count = 2;
            _bytes_to_send = _write_idx + _file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    _iv[0].iov_base = _write_buf;
    _iv[0].iov_len = _write_idx;
    _iv_count = 1;
    _bytes_to_send = _write_idx;
    return true;
}
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(_epollfd, _sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(_epollfd, _sockfd, EPOLLOUT);
}
