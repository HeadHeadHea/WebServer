#ifndef __LOCKER__H__
#define __LOCKER__H__

#include<unistd.h>
#include<pthread.h>
#include<exception>
#include<semaphore.h>
#include<sys/time.h>
#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<string.h>

//打印错误函数
#define printErr(str) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,str)
//打印系统调用错误函数
#define printSysErr(str,num) fprintf(stderr,"%s:%d:%s:%s\n",__FILE__,__LINE__,str,strerror(num))
//打印数据库错误函数
#define printSqlErr(str,con) fprintf(stderr,"%s:%d:%s:%s\n",__FILE__,__LINE__,str,mysql_error(con))

//互斥锁的封装
class Locker{
public:
    Locker();
    ~Locker();

    //加锁
    bool lock();
    //解锁
    bool unlock();
    //获取锁
    pthread_mutex_t& getMutex();
private:
    pthread_mutex_t m_mutex;
};

//条件变量的封装
class Cond{
public:
    Cond();
    ~Cond();

    //阻塞
    bool wait(pthread_mutex_t* mutex);
    //tv_sec,tv_nsec相对时间
    bool timedwait(pthread_mutex_t* mutex, long tv_sec, long tv_nsec);
    //唤醒
    bool signal();
    bool broadcast();
private:
    //设置超时时间
    void setTimeout(long tv_sec, long tv_nsec);

    pthread_cond_t m_cond;
    struct timespec m_timeout;
};


//信号量的封装
class Sem{
public:
    Sem();
    Sem(unsigned value);
    ~Sem();

    //阻塞
    bool wait();
    //唤醒
    bool post();
private:
    sem_t m_sem;
};

#endif