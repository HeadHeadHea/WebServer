#include "include/HttpConn.h"
#include <signal.h>
#include "include/TimerWheel.h"
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

using namespace std;

#define MAX_FD 1024
#define MAX_EVENTS 1024
#define TIME_SLOT 5

static int epollfd = -1;
static int pipefd[2];
static TimerWheel wheel(1, time(NULL));

void sig_handler(int sig)
{
	int save_errno = errno;
	char msg = sig;
	send(pipefd[1], &msg, 1, 0);
	errno = save_errno;
}

void add_sig(int sig)
{
	struct sigaction act;
	act.sa_handler = sig_handler;
	act.sa_flags |= SA_RESTART;
	sigfillset(&act.sa_mask);
	sigaction(sig, &act, NULL);
}

void timer_handler()
{
	wheel.wheelUpdate(time(NULL));
	printf("timer_handler\n");
	alarm(TIME_SLOT);
}

void cb_func(void *arg)
{
	HttpConn *http = (HttpConn *)arg;
	removefd(epollfd, http->getFd());
}

// 向连接对象发送错误消息
void show_error(int confd, const char *str)
{
	send(confd, str, strlen(str), 0);
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("please input server port\n");
		exit(1);
	}

	int port = atoi(argv[1]);

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenfd != -1);

	/* 避免TIME_WAIT状态，仅用于调试 */
	int reuse = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	struct sockaddr_in ser_addr;
	memset(&ser_addr, 0, sizeof(ser_addr));
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ser_addr.sin_port = htons(port);

	int ret = bind(listenfd, (struct sockaddr *)&ser_addr, sizeof(ser_addr));
	assert(ret != -1);

	ret = listen(listenfd, 5);
	assert(ret != -1);

	epollfd = epoll_create(1024);
	assert(epollfd != -1);

	struct epoll_event events[MAX_EVENTS];

	HttpConn::m_epollfd = epollfd;
	HttpConn *http_users = new HttpConn[MAX_FD];
	Timer *timers = new Timer[MAX_FD];

	Log *log = Log::getInstance();
	log->init("server");

	shared_ptr<SqlPool> sql_pool = SqlPool::getInstance();
	initMysqlResult(sql_pool);

	ThreadPool<HttpConn> *thread_pool = new ThreadPool<HttpConn>(sql_pool);

	ret = socketpair(AF_INET, SOCK_STREAM, 0, pipefd);
	setNonblocking(pipefd[1]);
	addfd(epollfd, pipefd[0], false);
	addfd(epollfd, listenfd, false);

	add_sig(SIGTERM);
	add_sig(SIGALRM);
	bool stop_server = false;
	bool timeout = false;

	while (!stop_server)
	{
		int num = epoll_wait(epollfd, events, 1024, -1);
		if (num == -1 && errno != EINTR)
		{
			printf("error\n");
			break;
		}
		for (int i = 0; i < num; ++i)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd)
			{
				struct sockaddr_in client_addr;
				socklen_t client_len = sizeof(client_addr);
				int confd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
				if (confd == -1)
				{
					printf("accept error\n");
					continue;
				}
				else if (HttpConn::m_userCount >= MAX_FD)
				{
					show_error(confd, "Internal server busy");
					continue;
				}
				http_users[confd].init(confd, client_addr);
				wheel.wheelNodeInit(timers + confd, cb_func, http_users + confd);
				wheel.setTimer(timers + sockfd, 2 * TIME_SLOT);
			}
			else if (sockfd == pipefd[0] && events[i].events & EPOLLIN)
			{
				int sig;
				char signals[1024];
				ret = recv(sockfd, signals, 1024, 0);
				if (ret == -1)
				{
					continue;
				}
				else if (ret == 0)
				{
					continue;
				}
				else
				{
					for (int i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
						case SIGTERM:
							timeout = true;
							break;
						case SIGALRM:
							stop_server = true;
							break;
						}
					}
				}
			}
			else if (events[i].events & EPOLLIN)
			{
				if (http_users[sockfd].readn())
				{
					thread_pool->append(&http_users[sockfd]);
				}
				else
				{
					http_users[sockfd].closeConn();
				}
			}
			else if (events[i].events & EPOLLOUT)
			{
				if (!http_users[sockfd].writen())
				{
					http_users[sockfd].closeConn();
				}
			}
			if(timeout){
				timer_handler();
				timeout = false;
			}
		}
	}
	delete thread_pool;
	delete[] http_users;
	close(epollfd);
	close(listenfd);
	delete[] timers;

	return 0;
}