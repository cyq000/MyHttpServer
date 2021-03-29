#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <stdio.h>
#include <exception>
#include <pthread.h>
#include "../lock/locker.hpp"
#include "../mysql/mysqlpool.hpp"

template <typename T>
class threadpool
{
public:
    //thread_number是线程池中线程数量，max_requests是请求队列中最多允许的数量
    threadpool(Mysql_conection_pool *connPool, int thread_number = 4, int max_request = 10000);
    ~threadpool();
    bool append(T *request);

private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void *worker(void *arg);
    void run();

private:
    int _threadNumber;        //线程池中的线程数
    int _maxRequests;         //请求队列中允许的最大请求数
    pthread_t *_threads;       //描述线程池的数组，其大小为_thread_number
    std::list<T *> _workQueue; //请求队列
    locker _queueLocker;       //保护请求队列的互斥锁
    sem _queueStat;            //是否有任务需要处理
    bool _stop;                //是否结束线程
    Mysql_conection_pool *_connPool;  //数据库
    MYSQL *_mysql;
};
template <typename T>
threadpool<T>::threadpool(Mysql_conection_pool *connPool, int thread_number, int max_requests) : _threadNumber(thread_number), _maxRequests(max_requests), _stop(false), _threads(NULL),_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    _threads = new pthread_t[thread_number];
    if (!_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        //printf("create the %dth thread\n",i);
        if (pthread_create(_threads + i, NULL, worker, this) != 0)
        {
            delete[] _threads;
            throw std::exception();
        }
        if (pthread_detach(_threads[i]))
        {
            delete[] _threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] _threads;
    _stop = true;
}
template <typename T>
bool threadpool<T>::append(T *request)
{
    _queueLocker.lock();
    if (_workQueue.size() > _maxRequests)
    {
        _queueLocker.unlock();
        return false;
    }
	_workQueue.push_back(request);
    _queueLocker.unlock();
    _queueStat.post();
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (!_stop)
    {
        _queueStat.wait();
        _queueLocker.lock();
        if (_workQueue.empty())
        {
            _queueLocker.unlock();
            continue;
        }
        T *request = _workQueue.front();
        _workQueue.pop_front();
        _queueLocker.unlock();
        if (!request)
            continue;

        connectionRAII mysqlcon(&request->_mysql, _connPool);
        
        request->process();
    }
}
#endif
