/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.hpp"
using namespace std;

template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
    {
        if (max_size <= 0)
        {
            exit(-1);
        }

        _max_size = max_size;
        _array = new T[max_size];
        _size = 0;
        _front = -1;
        _back = -1;
    }

    void clear()
    {
        _mutex.lock();
        _size = 0;
        _front = -1;
        _back = -1;
        _mutex.unlock();
    }

    ~block_queue()
    {
        _mutex.lock();
        if (_array != NULL)
            delete [] _array;

        _mutex.unlock();
    }
    //判断队列是否满了
    bool full() 
    {
        _mutex.lock();
        if (_size >= _max_size)
        {

            _mutex.unlock();
            return true;
        }
        _mutex.unlock();
        return false;
    }
    //判断队列是否为空
    bool empty() 
    {
        _mutex.lock();
        if (0 == _size)
        {
            _mutex.unlock();
            return true;
        }
        _mutex.unlock();
        return false;
    }
    //返回队首元素
    bool front(T &value) 
    {
        _mutex.lock();
        if (0 == _size)
        {
            _mutex.unlock();
            return false;
        }
        value = _array[_front];
        _mutex.unlock();
        return true;
    }
    //返回队尾元素
    bool back(T &value) 
    {
        _mutex.lock();
        if (0 == _size)
        {
            _mutex.unlock();
            return false;
        }
        value = _array[_back];
        _mutex.unlock();
        return true;
    }

    int size() 
    {
        int tmp = 0;

        _mutex.lock();
        tmp = _size;

        _mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        int tmp = 0;

        _mutex.lock();
        tmp = _max_size;

        _mutex.unlock();
        return tmp;
    }
    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)
    {

        _mutex.lock();
        if (_size >= _max_size)
        {

            _cond.broadcast();
            _mutex.unlock();
            return false;
        }

        _back = (_back + 1) % _max_size;
        _array[_back] = item;

        _size++;

        _cond.broadcast();
        _mutex.unlock();
        return true;
    }
    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {

        _mutex.lock();
        while (_size <= 0)
        {
            
            if (!_cond.wait(_mutex.get()))
            {
                _mutex.unlock();
                return false;
            }
        }

        _front = (_front + 1) % _max_size;
        item = _array[_front];
        _size--;
        _mutex.unlock();
        return true;
    }

private:
    locker _mutex;
    cond _cond;

    T *_array;
    int _size;
    int _max_size;
    int _front;
    int _back;
};

#endif
