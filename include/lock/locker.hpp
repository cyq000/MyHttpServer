#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        if (sem_init(&_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&_sem);
    }
    bool wait()     //阻塞等待p:-1
    {
        return sem_wait(&_sem) == 0;
    }
    bool post()     //v:+1
    {
        return sem_post(&_sem) == 0;
    }

private:
    sem_t _sem;
};
class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&_mutex) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&_mutex) == 0;
    }
    pthread_mutex_t *get()
    {
        return &_mutex;
    }

private:
    pthread_mutex_t _mutex;
};
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&_cond);
    }
    bool wait(pthread_mutex_t *_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&_cond, _mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    bool signal()
    {
        return pthread_cond_signal(&_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t _cond;
};
#endif
