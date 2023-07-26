#ifndef __LOG__H__
#define __LOG__H__

#include <stdarg.h>
#include "BlockQueue.h"
#include <string>
#include "Locker.h"

/* 单例模式实现的日志类 */
class Log
{
public:
	/* 防止对象的拷贝 */
	Log(const Log &log) = delete;
	Log &operator=(const Log &log) = delete;

	~Log();
	/* 日志打印级别 */
	enum Level
	{
		DEBUG = 1,
		INFO,
		WARN,
		ERROR
	};
	/* 获取该类的全局唯一实例 */
	static Log *getInstance();
	/* 工作线程的回调函数 */
	static void *flushLogThread(void *arg);
	/* 可选择的参数：日志文件、日志缓冲区大小、最大行数以及最长日志条队列 */
	bool init(const char *file_name, int log_buffer_size = 8192, int max_lines = 50000, int max_queue_size = 1000000);
	/* 以指定格式打印日志 */
	void writeLog(Level level, const char *ev_file_name, int ev_line, const char *format, ...);
	/* 强制刷新到磁盘 */
	void flush();

private:
	/* 私有化构造函数 */
	Log(){};
	/* 资源释放函数*/
	void logFree();

private:
	/* 日志所在文件夹路径名 */
	char m_dir_name[128] = {0};
	/* 日志文件名 */
	char m_log_name[128] = {0};
	/* 日志最大行数 */
	int m_max_lines;
	/* 日志缓冲区大小 */
	int m_log_buffer_size;
	/* 日志行数记录 */
	long long m_count = 0;
	/* 事件文件名*/
	char m_ev_file_name[256] = {0};
	/* 事件行号*/
	int m_ev_line;
	/* 日志按天进行打印，用来记录当前是哪一天 */
	int m_today;
	/* 指向日志文件的文件指针 */
	FILE *m_fp;
	/* 日志缓冲区首地址 */
	char *m_buffer;
	/* 线程安全的阻塞队列 */
	BlockQueue<std::string> *m_log_queue;
	/* 互斥锁 */
	Locker m_mutex;
	pthread_t m_tid = 0;
	/* 关闭标志*/
	bool m_shutdown = false;
};

#define LOG_DEBUG(format, ...) \
	Log::getInstance()->writeLog(Log::DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define LOG_INFO(format, ...) \
	Log::getInstance()->writeLog(Log::INFO, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define LOG_WARN(format, ...) \
	Log::getInstance()->writeLog(Log::WARN, __FILE__, __LINE__, format, ##__VA_ARGS__);

#define LOG_ERROR(format, ...) \
	Log::getInstance()->writeLog(Log::ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__);

#endif