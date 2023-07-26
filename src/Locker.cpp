#include"../include/Locker.h"

Locker::Locker(){
    int ret = pthread_mutex_init(&m_mutex, NULL);
    if(ret != 0){
        //printSysErr("Locker::pthread_mutex_init error",ret);
        throw std::exception();
    }
}

Locker::~Locker(){
    int ret = pthread_mutex_destroy(&m_mutex);
    if(ret != 0){
        printSysErr("Locker::pthread_mutex_destroy error",ret);
    }
}

bool Locker::lock(){
    int ret = pthread_mutex_lock(&m_mutex);
    if(ret != 0){
        //printSysErr("Locker::pthread_mutex_lock error",ret);
        return false;
    }
    return true;
}

bool Locker::unlock(){
    int ret = pthread_mutex_unlock(&m_mutex);
    if(ret != 0){
        //printSysErr("Locker::pthread_mutex_unlock error",ret);
        return false;
    }
    return true;
}

pthread_mutex_t& Locker::getMutex(){
    return m_mutex;
}

//*************************************************
Cond::Cond(){
    int ret = pthread_cond_init(&m_cond, NULL);
    if(ret != 0){
        //printSysErr("Cond::pthread_cond_init error",ret);
        throw std::exception();
    }
}

Cond::~Cond(){
    int ret = pthread_cond_destroy(&m_cond);
    if(ret != 0){
        printSysErr("Cond::pthread_cond_destroy error",ret);
    }
}

bool Cond::wait(pthread_mutex_t* mutex){
    int ret = pthread_cond_wait(&m_cond, mutex);
    if(ret != 0){
        //printSysErr("Cond::pthread_cond_wait error",ret);
        return false;
    }
    return true;
}

void Cond::setTimeout(long tv_sec, long tv_nsec){
    struct timeval now;
    gettimeofday(&now, NULL);
    m_timeout.tv_sec = now.tv_sec + tv_sec;
    m_timeout.tv_nsec = now.tv_usec * 1000 + tv_nsec;
}

bool Cond::timedwait(pthread_mutex_t* mutex, long tv_sec, long tv_nsec){
    setTimeout(tv_sec, tv_nsec);
    int ret = pthread_cond_timedwait(&m_cond, mutex, &m_timeout);
    if(ret != 0){
        //printSysErr("Cond::pthread_cond_timedwait error",ret);
        return false;
    }
    return true;
}

bool Cond::signal(){
    int ret = pthread_cond_signal(&m_cond);
    if(ret != 0){
        //printSysErr("Cond::pthread_cond_signal error",ret);
        return false;
    }
    return true;
}
bool Cond::broadcast(){
    int ret = pthread_cond_broadcast(&m_cond);
    if(ret != 0){
        //printSysErr("Cond::pthread_cond_broadcast error",ret);
        return false;
    }
    return true;
}

//=================================================
Sem::Sem(){
    int ret = sem_init(&m_sem, 0, 0);
    if(ret != 0){
        //printSysErr("Sem::sem_init error",ret);
        throw std::exception();
    }
}

Sem::Sem(unsigned value){
    int ret = sem_init(&m_sem, 0, value);
    if(ret != 0){
        //printSysErr("Sem::sem_init error",ret);
        throw std::exception();
    }
}

Sem::~Sem(){
    int ret = sem_destroy(&m_sem);
    if(ret != 0){
        printSysErr("Sem::sem_destroy error",ret);
    }
}

bool Sem::wait(){
    int ret = sem_wait(&m_sem);
    if(ret != 0){
        //printSysErr("Sem::sem_wait error",ret);
        return false;
    }
    return true;
}

bool Sem::post(){
    int ret = sem_post(&m_sem);
    if(ret != 0){
        //printSysErr("Sem::sem_post error",ret);
        return false;
    }
    return true;
}