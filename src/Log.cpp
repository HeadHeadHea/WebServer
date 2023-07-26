#include <ctime>
#include "../include/Log.h"

/* 使用线程安全的局部静态对象*/
Log *Log::getInstance()
{
	static Log instance;
	return &instance;
}

/* 工作线程的回调函数 */
void *Log::flushLogThread(void *arg)
{
	Log *log = (Log *)arg;
	while (!log->m_shutdown)
	{
		std::string record;
		/* 从阻塞队列中取出一个日志string, 写入文件 */
		while (log->m_log_queue->pop(record, 5000))
		{
			log->m_mutex.lock();
			fputs(record.c_str(), log->m_fp);
			log->m_mutex.unlock();
		}
	}
	const char* str = "Log::flushLogThread exit success";
	return (void*)str;
}

void Log::logFree()
{
	m_shutdown = true;
	// 回收线程
	if (m_tid != 0)
	{
		char *status;
		int ret = pthread_join(m_tid, (void **)&status);
		if (ret != 0)
		{
			printSysErr("Log::pthread_join error", ret);
		}
		printf("Log::flushLogThread pthread_join success: %lu status: %s\n", m_tid, status);
	}
	// 将队列剩余资源写入文件
	std::string record;
	while (m_log_queue->pop(record, 1000))
	{
		fputs(record.c_str(), m_fp);
	}
	// 销毁队列，缓冲区，关闭文件流
	if (m_log_queue)
	{
		delete m_log_queue;
	}
	if (m_buffer)
	{
		delete[] m_buffer;
	}
	if (m_fp)
	{
		fclose(m_fp);
	}
	printf("Log::logFree success\n");
}

Log::~Log()
{
	logFree();
}

bool Log::init(const char *file_name, int log_buffer_size, int max_lines, int max_queue_size)
{
	// 初始化队列
	m_log_queue = new (std::nothrow) BlockQueue<std::string>(max_queue_size);
	if (m_log_queue == nullptr)
	{
		throw std::exception();
	}

	// 创建线程
	int ret = pthread_create(&m_tid, nullptr, flushLogThread, (void *)this);
	if (ret != 0)
	{
		printSysErr("Log::pthread_create error", ret);
		logFree();
		throw std::exception();
	}

	// 初始化缓冲区
	m_log_buffer_size = log_buffer_size;
	m_buffer = new (std::nothrow) char[log_buffer_size];
	if (m_buffer == nullptr)
	{
		logFree();
		throw std::exception();
	}
	memset(m_buffer, '\0', m_log_buffer_size);

	// 初始化最大行数
	m_max_lines = max_lines;

	// 初始化时间
	time_t t = time(nullptr);
	struct tm *sysTm = localtime(&t);
	struct tm myTm = *sysTm;
	m_today = myTm.tm_mday;

	/* 指针p指向'/'最后一次出现的位置 */
	const char *p = strrchr(file_name, '/');
	char log_file_name[300] = {0};

	if (p == nullptr)
	{
		char* p = get_current_dir_name();
		strcpy(m_dir_name, p);
		m_dir_name[strlen(p)] = '/';
		strcpy(m_log_name, file_name);
		snprintf(log_file_name, 300, "%s%d_%02d_%02d_%s", m_dir_name, myTm.tm_year + 1900,
				 myTm.tm_mon + 1, myTm.tm_mday, m_log_name);
		free(p);
	}
	else
	{
		strcpy(m_log_name, p + 1);
		strncpy(m_dir_name, file_name, p - file_name + 1);
		snprintf(log_file_name, 300, "%s%d_%02d_%02d_%s", m_dir_name, myTm.tm_year + 1900,
				 myTm.tm_mon + 1, myTm.tm_mday, m_log_name);
	}
	
	m_fp = fopen(log_file_name, "a");
	if (m_fp == nullptr)
	{
		logFree();
		return false;
	}
	return true;
}

void Log::writeLog(Level level, const char *ev_file_name, int ev_line, const char *format, ...)
{
	struct timeval now = {0, 0};
	gettimeofday(&now, nullptr);
	time_t t = now.tv_sec;
	struct tm *sysTm = localtime(&t);
	struct tm myTm = *sysTm;
	char s[16] = {0};

	switch (level)
	{
	case DEBUG:
		strcpy(s, "[debug]:");
		break;
	case INFO:
		strcpy(s, "[info]:");
		break;
	case WARN:
		strcpy(s, "[warn]:");
		break;
	case ERROR:
		strcpy(s, "[error]:");
		break;
	default:
		strcpy(s, "[info]:");
		break;
	}

	/* 写入一个log，对m_count++ */
	m_mutex.lock();
	m_count++;
	/* 若到了新的一天或当前行数超过了最大行数，则将日志打印在新的文件中 */
	if (m_today != myTm.tm_mday || m_count % m_max_lines == 0)
	{
		char newLog[301] = {0};
		fflush(m_fp);
		fclose(m_fp);
		char tail[16] = {0};
		snprintf(tail, 16, "%d_%02d_%02d_", myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday);
		if (m_today != myTm.tm_mday)
		{
			snprintf(newLog, 300, "%s%s%s", m_dir_name, tail, m_log_name);
			m_today = myTm.tm_mday;
			m_count = 0;
		}
		else
		{
			snprintf(newLog, 300, "%s%s%s.%lld", m_dir_name, tail, m_log_name, m_count / m_max_lines);
		}
		m_fp = fopen(newLog, "a");
	}
	m_mutex.unlock();

	va_list valst;
	va_start(valst, format);
	std::string logStr;

	m_mutex.lock();
	strcpy(m_ev_file_name, ev_file_name);
	m_ev_line = ev_line;
	int n = snprintf(m_buffer, m_log_buffer_size, "%d-%02d-%02d %02d:%02d:%02d %s %s:%d ",
					 myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday,
					 myTm.tm_hour, myTm.tm_min, myTm.tm_sec, s, m_ev_file_name, m_ev_line);
	int m = vsnprintf(m_buffer + n, m_log_buffer_size - n, format, valst);
	m_buffer[n + m] = '\n';
	m_buffer[n + m + 1] = '\0';
	logStr = m_buffer;
	m_mutex.unlock();

	if (!m_log_queue->isFull())
	{
		m_log_queue->push(logStr);
	}
	va_end(valst);
}

void Log::flush(void)
{
	m_mutex.lock();
	fflush(m_fp);
	m_mutex.unlock();
}
