#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "blockqueue.hpp"

using namespace std;

class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (_log_queue->pop(single_log))
        {
            _mutex.lock();
            fputs(single_log.c_str(), _fp);
            _mutex.unlock();
        }
    }

private:
    char _dir_name[256]; //路径名
    char _log_name[256]; //log文件名
    int _split_lines;  //日志最大行数
    int _log_buf_size; //日志缓冲区大小
    long long _count;  //日志行数记录
    int _today;        //因为按天分类,记录当前时间是那一天
    FILE *_fp;         //打开log的文件指针
    char *_buf;
    block_queue<string> *_log_queue; //阻塞队列
    bool _is_async;                  //是否同步标志位
    locker _mutex;
};


#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif
