#ifndef __BLOCK__QUEUE__
#define __BLOCK__QUEUE__

#include "Locker.h"
#include <new>

// 实现线程安全的队列
template <typename T>
class BlockQueue
{
public:
	BlockQueue(int maxsize);
	~BlockQueue();

	/* 清空队列 */
	void clear();
	/* 判断队列是否满了 */
	bool isFull();
	/* 判断队列是否为空 */
	bool isEmpty();
	/* 返回队首元素 */
	bool front(T &value);
	/* 返回队尾元素 */
	bool back(T &value);
	/* 获取当前队列大小 */
	int size();
	/* 获取队列的容量 */
	int capacity();
	/* 向队列中添加元素 */
	bool push(const T &item);
	/* 弹出队首元素，若队列为空，则等待条件变量 */
	bool pop(T &item);
	/* 弹出队首元素，若队列为空，超时处理 */
	bool pop(T &item, int ms_timeout);

private:
	// 使用条件变量
	Locker m_mutex;
	Cond m_cond;
	T *m_array;
	// 元素数量
	int m_size;
	// 队列容量
	int m_capacity;
	// 队头指针
	int m_front;
	// 尾后指针
	int m_back;
};

template <typename T>
BlockQueue<T>::BlockQueue(int maxsize)
{
	if (maxsize <= 0)
	{
		throw std::exception();
	}

	m_capacity = maxsize;
	m_array = new (std::nothrow) T[maxsize];
	if (m_array == nullptr)
	{
		throw std::exception();
	}
	m_size = 0;
	m_front = 0;
	m_back = 0;
}

template <typename T>
BlockQueue<T>::~BlockQueue()
{
	delete[] m_array;
	m_array = nullptr;
}

template <typename T>
void BlockQueue<T>::clear()
{
	m_mutex.lock();
	m_size = 0;
	m_front = 0;
	m_back = 0;
	m_mutex.unlock();
}

template <typename T>
bool BlockQueue<T>::isFull()
{
	m_mutex.lock();
	if (m_size >= m_capacity)
	{
		m_mutex.unlock();
		return true;
	}
	m_mutex.unlock();
	return false;
}

template <typename T>
bool BlockQueue<T>::isEmpty()
{
	m_mutex.lock();
	if (m_size <= 0)
	{
		m_mutex.unlock();
		return true;
	}
	m_mutex.unlock();
	return false;
}

template <typename T>
bool BlockQueue<T>::front(T &value)
{
	m_mutex.lock();
	if (m_size <= 0)
	{
		m_mutex.unlock();
		return false;
	}
	value = m_array[m_front];
	m_mutex.unlock();
	return true;
}

template <typename T>
bool BlockQueue<T>::back(T &value)
{
	m_mutex.lock();
	if (m_size <= 0)
	{
		m_mutex.unlock();
		return false;
	}
	value = m_array[m_back - 1];
	m_mutex.unlock();
	return true;
}

template <typename T>
int BlockQueue<T>::size()
{
	int tmp = 0;
	m_mutex.lock();
	tmp = m_size;
	m_mutex.unlock();
	return tmp;
}

template <typename T>
int BlockQueue<T>::capacity()
{
	int tmp = 0;
	m_mutex.lock();
	tmp = m_capacity;
	m_mutex.unlock();
	return tmp;
}

template <typename T>
bool BlockQueue<T>::push(const T &item)
{
	m_mutex.lock();
	/* 队列已满，直接返回，丢弃该日志*/
	if (m_size >= m_capacity)
	{
		m_cond.broadcast();
		m_mutex.unlock();
		return false;
	}

	m_array[m_back] = item;
	m_back = (m_back + 1) % m_capacity;
	++m_size;
	m_cond.broadcast();
	m_mutex.unlock();
	return true;
}

template <typename T>
bool BlockQueue<T>::pop(T &item)
{
	/* 弹出队首元素时，若队列为空，将会等待条件变量 */
	m_mutex.lock();
	while (m_size <= 0)
	{
		if (!m_cond.wait(&m_mutex.getMutex()))
		{
			m_mutex.unlock();
			return false;
		}
	}

	item = m_array[m_front];
	m_front = (m_front + 1) % m_capacity;
	--m_size;
	m_mutex.unlock();
	return true;
}

template <typename T>
bool BlockQueue<T>::pop(T &item, int ms_timeout)
{
	m_mutex.lock();
	if (m_size <= 0)
	{
		long tv_sec = ms_timeout / 1000;
		long tv_nsec = (ms_timeout % 1000) * 1000000;
		if (!m_cond.timedwait(&m_mutex.getMutex(), tv_sec, tv_nsec))
		{
			m_mutex.unlock();
			return false;
		}
	}

	/* 若超时时间到时队列仍为空，则解锁后返回 */
	if (m_size <= 0)
	{
		m_mutex.unlock();
		return false;
	}
	/* 否则弹出队首元素 */
	item = m_array[m_front];
	m_front = (m_front + 1) % m_capacity;
	--m_size;
	m_mutex.unlock();
	return true;
}


#endif