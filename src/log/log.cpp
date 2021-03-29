#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.hpp"
#include <pthread.h>
using namespace std;

Log::Log()
{
    _count = 0;
    _is_async = false;
}

Log::~Log()
{
    if (_fp != NULL)
    {
        fclose(_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        _is_async = true;
        _log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    _log_buf_size = log_buf_size;
    _buf = new char[_log_buf_size];
    memset(_buf, '\0', _log_buf_size);
    _split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

 
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(_log_name, p + 1);
        strncpy(_dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", _dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, _log_name);
    }

    _today = my_tm.tm_mday;

    _fp = fopen(log_full_name, "a");
    if (_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对_count++, _split_lines最大行数
    _mutex.lock();
    _count++;

    if (_today != my_tm.tm_mday || _count % _split_lines == 0) //everyday log
    {
        
        char new_log[256] = {0};
        fflush(_fp);
        fclose(_fp);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", _dir_name, tail, _log_name);
            _today = my_tm.tm_mday;
            _count = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", _dir_name, tail, _log_name, _count / _split_lines);
        }
        _fp = fopen(new_log, "a");
    }
 
    _mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    _mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(_buf + n, _log_buf_size - 1, format, valst);
    _buf[n + m] = '\n';
    _buf[n + m + 1] = '\0';
    log_str = _buf;

    _mutex.unlock();

    if (_is_async && !_log_queue->full())
    {
        _log_queue->push(log_str);
    }
    else
    {
        _mutex.lock();
        fputs(log_str.c_str(), _fp);
        _mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    _mutex.lock();
    //强制刷新写入流缓冲区
    fflush(_fp);
    _mutex.unlock();
}
