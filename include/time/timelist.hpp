#ifndef _TIMELISt_H
#define _TIMELIST_H

#include <time.h>
#include "../log/log.hpp"

class UtilTimer;
struct client_data
{
    sockaddr_in _address;
    int _sockfd;
    UtilTimer *_timer;
};

class UtilTimer
{
public:
    UtilTimer() : prev(NULL), next(NULL) {}

public:
	void (*cb_func)(client_data *);

    time_t _expire;    
    client_data *_user_data;
    UtilTimer *prev;
    UtilTimer *next;
};

class SortTimerList 
{
public:
    SortTimerList() : head(NULL), tail(NULL) {}
    ~SortTimerList()
    {
        UtilTimer *tmp = head;
        while (tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    void add_timer(UtilTimer *timer)
    {
        if (!timer)
        {
            return;
        }
        if (!head)
        {
            head = tail = timer;
            return;
        }
        if (timer->_expire < head->_expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }
    void adjust_timer(UtilTimer *timer)
    {
        if (!timer)
        {
            return;
        }
        UtilTimer *tmp = timer->next;
        if (!tmp || (timer->_expire < tmp->_expire))
        {
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    void del_timer(UtilTimer *timer)
    {
        if (!timer)
        {
            return;
        }
        if ((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    void tick()
    {
        if (!head)
        {
            return;
        }
        //printf( "timer tick\n" );
        LOG_INFO("%s", "timer tick");
        Log::get_instance()->flush();
        time_t cur = time(NULL);
        UtilTimer *tmp = head;
        while (tmp)
        {
            if (cur < tmp->_expire)
            {
                break;
            }
            tmp->cb_func(tmp->_user_data);
            head = tmp->next;
            if (head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(UtilTimer *timer, UtilTimer *lst_head)
    {
        UtilTimer *prev = lst_head;
        UtilTimer *tmp = prev->next;
        while (tmp)
        {
            if (timer->_expire < tmp->_expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    UtilTimer *head;
    UtilTimer *tail;
};

#endif
