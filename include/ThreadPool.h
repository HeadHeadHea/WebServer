#ifndef __THREAD__POOL__
#define __THREAD__POOL__

#include"Locker.h"
#include"SqlPool.h"
#include<list>
#include<new>


//定义线程池类
template <typename T>
class ThreadPool
{
public:
    ThreadPool(std::shared_ptr<SqlPool> connPool, int threadNumber = 8, int maxRequests = 10000);
	~ThreadPool();
	/* 向请求队列中添加任务 */
	bool append(T* request);
private:
    /* 工作线程运行的函数, 它不断从工作队列中取出任务并执行之 */
	static void* worker(void* arg);
	void run();
private:
    /* 线程池中的线程数 */
	int m_threadNumber;
	/* 请求队列中允许的最大请求数 */
	int m_maxRequests;
	/* 描述线程池的数组, 其大小为m_threadNumber */
	pthread_t* m_threads;
	/* 请求队列 */
	std::list<T*> m_workQueue;
	/* 保护请求队列的互斥锁 */
	Locker m_queueLocker;
	/* 是否有任务需要处理 */
	Sem m_queueStat;
	/* 是否结束线程 */
	bool m_stop;
	/* 数据库连接池 */
	std::shared_ptr<SqlPool> m_connPool;
};


template<typename T>
ThreadPool<T>::ThreadPool(std::shared_ptr<SqlPool> connPool, int threadNumber, int maxRequests)
{
	if (threadNumber <= 0 || maxRequests <= 0) {
		throw std::exception();
	}

	/* 成员变量初始化 */
	m_threadNumber = threadNumber;
	m_maxRequests = maxRequests;
	m_stop = false;
	m_threads = new pthread_t[m_threadNumber];
	m_connPool = connPool;

	/* 创建threadNumber个线程, 并设置线程分离 */
	for (int i = 0; i < threadNumber; ++i) {
		if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
			delete [] m_threads;
			throw std::exception();
		}
	}
}


template<typename T>
ThreadPool<T>::~ThreadPool()
{
    //回收线程
    m_stop = true;
    for(int i = 0; i < m_threadNumber; ++i){
        if(pthread_join(m_threads[i], NULL) != 0){
            printf("pthread_join m_threads[%d]: %lu error\n", i, m_threads[i]);
        }
    }
	delete [] m_threads;
}

template<typename T>
bool ThreadPool<T>::append(T* request)
{
	//如果任务队列已满，则添加失败
	m_queueLocker.lock();
	if (m_workQueue.size() >= m_maxRequests) {
		m_queueLocker.unlock();
		return false;
	}
	m_workQueue.push_back(request);
	m_queueLocker.unlock();
	//唤醒工作线程
	m_queueStat.post();
	return true;
}

template<typename T>
void* ThreadPool<T>::worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	pool->run();
	return pool;
}

template<typename T>
void ThreadPool<T>::run()
{
	while (!m_stop) {
		m_queueStat.wait();
		m_queueLocker.lock();
		T* request = m_workQueue.front();
		m_workQueue.pop_front();
		m_queueLocker.unlock();
		if (!request) {
			continue;
		}
		//从数据库连接池中获取连接
		std::shared_ptr<SqlCon> sql_con = m_connPool->getRecyCon();
		request->setSqlCon(sql_con);
		//httpconn类必须实现process函数
		request->process();
	}
}

#endif