#include "../include/HttpConn.h"

/* 定义HTTP响应的一些状态信息 */
const char *OK_200_TITLE = "OK";
const char *ERROR_400_TITLE = "Bad Request";
const char *ERROR_400_FORM = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *ERROR_403_TITLE = "Forbidden";
const char *ERROR_403_FORM = "You do not have permission to get file from this server.\n";
const char *ERROR_404_TITLE = "Not Found";
const char *ERROR_404_FORM = "The requested file was not found on this server.\n";
const char *ERROR_500_TITLE = "Internal Error";
const char *ERROR_500_FORM = "There was an unusual problem serving the requested file.\n";

// 全局锁
Locker m_lock;

/* 类的静态成员变量必须在类外初始化 */
int HttpConn::m_userCount = 0;
int HttpConn::m_epollfd = -1;
std::unordered_map<std::string, std::string> m_users;

/* 将文件描述符设置为非阻塞 */
int setNonblocking(int fd)
{
	int oldOption = fcntl(fd, F_GETFL);
	int newOption = oldOption | O_NONBLOCK;
	fcntl(fd, F_SETFL, newOption);
	return oldOption;
}

/* 向epoll内核事件表中注册文件描述符事件 */
void addfd(int epollfd, int fd, bool oneShot)
{
	struct epoll_event event;
	event.data.fd = fd;

	// epoll边沿触发
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if (oneShot)
	{
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setNonblocking(fd);
}

/* 向epoll内核事件表中删除文件描述符所注册的事件 */
void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);
}

/* 向epoll内核事件表中修改文件描述符所注册的事件*/
void modfd(int epollfd, int fd, int ev)
{
	struct epoll_event event;
	event.data.fd = fd;

	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void initMysqlResult(std::shared_ptr<SqlPool> connPool)
{
	/* 从数据库中取出一个连接 */
	std::shared_ptr<SqlCon> mysql;
	mysql = connPool->getRecyCon();

	/* 从webserver表中检索username, passwd数据 */
	mysql->query("select user, passwd from webserver");

	/* 利用哈希表记录下所有客户的用户名和密码 */
	while (mysql->getRow())
	{
		std::string name = mysql->getField(0);
		std::string pwd = mysql->getField(1);
		m_users[name] = pwd;
		//printf("name = %s\n", name.c_str());
		//printf("pwd = %s\n", pwd.c_str());
	}
}


/* 关闭连接*/
void HttpConn::closeConn(bool realClose)
{
	if (realClose && m_sockfd != -1)
	{
		removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		/* 关闭一个连接时, 将客户总量减1 */
		m_userCount--;
		printf("close %d\n", m_sockfd);
	}
}

void HttpConn::init(int sockfd, const sockaddr_in &addr, const char* root)
{
	m_sockfd = sockfd;
	m_address = addr;
	m_docRoot = root;
	addfd(m_epollfd, sockfd, true);
	m_userCount++;

	init();
}

void HttpConn::init()
{
	// 状态机的默认状态
	m_checkState = CHECK_STATE_REQUESTLINE;
	m_linger = false;
	m_method = GET;
	m_url = nullptr;
	m_version = nullptr;
	m_contentLength = 0;
	m_host = nullptr;
	m_startLine = 0;
	m_checkedIdx = 0;
	m_readIdx = 0;
	m_writeIdx = 0;
	m_bytesToSend = 0;
	m_bytesHaveSend = 0;
	memset(m_readBuf, 0, READ_BUFFER_SIZE);
	memset(m_writeBuf, 0, WRITE_BUFFER_SIZE);
	memset(m_realFile, 0, FILENAME_LEN);
}

sockaddr_in *HttpConn::getAddress()
{
	return &m_address;
}

/* 读取完整的一行 */
HttpConn::LINE_STATUS HttpConn::parseLine()
{
	char temp;
	for (; m_checkedIdx < m_readIdx; ++m_checkedIdx)
	{
		temp = m_readBuf[m_checkedIdx];
		if (temp == '\r')
		{
			// 没有读到完整的一行继续读取
			if (m_checkedIdx + 1 == m_readIdx)
			{
				return LINE_OPEN;
			}
			// 读取到了一整行
			else if (m_readBuf[m_checkedIdx + 1] == '\n')
			{
				m_readBuf[m_checkedIdx++] = '\0';
				m_readBuf[m_checkedIdx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		else if (temp == '\n')
		{
			// 读取到了一整行
			if (m_checkedIdx > 1 && m_readBuf[m_checkedIdx - 1] == '\r')
			{
				m_readBuf[m_checkedIdx - 1] = '\0';
				m_readBuf[m_checkedIdx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;
}

/* 循环读取客户数据, 直到无数据可读或者对方关闭连接 */
bool HttpConn::readn()
{
	// 缓冲区已满
	if (m_readIdx >= READ_BUFFER_SIZE)
	{
		return false;
	}
	int bytesRead = 0;

	/* ET模式下读取数据循环读取 */
	while (true)
	{
		bytesRead = recv(m_sockfd, m_readBuf + m_readIdx, READ_BUFFER_SIZE - m_readIdx, 0);
		if (bytesRead == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				break;
			}
			return false;
		}
		else if (bytesRead == 0)
		{
			return false;
		}
		m_readIdx += bytesRead;
	}
	return true;
}

/* 解析HTTP请求行，获得请求方法、目标URL、以及HTTP版本号 */
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char *text)
{
	// 请求行没有空格或者制表符，则请求行有问题
	m_url = strpbrk(text, " \t");
	if (m_url == nullptr)
	{
		return BAD_REQUEST;
	}
	*m_url++ = '\0';

	// 只支持get和post请求
	char *method = text;
	if (strcasecmp(method, "GET") == 0)
	{
		m_method = GET;
	}
	else if (strcasecmp(method, "POST") == 0)
	{
		m_method = POST;
	}
	else
	{
		return BAD_REQUEST;
	}

	/* 跳过中间的空格和制表符 */
	m_url += strspn(m_url, " \t");
	m_version = strpbrk(m_url, " \t");

	if (m_version == nullptr)
	{
		return BAD_REQUEST;
	}
	*m_version++ = '\0';

	m_version += strspn(m_version, " \t");

	// 只支持http1.1
	if (strcasecmp(m_version, "HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}
	if (strncasecmp(m_url, "http://", 7) == 0)
	{
		m_url += 7;
		m_url = strchr(m_url, '/');
	}
	if (strncasecmp(m_url, "https://", 8) == 0)
	{
		m_url += 8;
		m_url = strchr(m_url, '/');
	}

	// 没有根目录，则url出错
	if (!m_url || m_url[0] != '/')
	{
		return BAD_REQUEST;
	}
	/* 设置url地址为/时的默认访问界面 */
	if (strlen(m_url) == 1)
	{
		strcat(m_url, "judge.html");
	}

	// 改变状态机的状态
	m_checkState = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

/* 解析HTTP请求的一个头部信息 */
HttpConn::HTTP_CODE HttpConn::parseHeaders(char *text)
{
	/* 遇到空行，表示头部字段解析完毕 */
	if (text[0] == '\0')
	{
		/* 如果HTTP请求还有请求体，则状态机转移到CHECK_STATE_CONTENT */
		if (m_contentLength != 0)
		{
			m_checkState = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		/* 否则说明我们已经得到了一个完整的HTTP请求 */
		return GET_REQUEST;
	}
	/* 处理Connection头部字段 */
	else if (strncasecmp(text, "Connection:", 11) == 0)
	{
		text += 11;
		text += strspn(text, " \t");
		// 使用持续连接
		if (strcasecmp(text, "keep-alive") == 0)
		{
			m_linger = true;
		}
	}
	/* 处理Content-Length头部字段 */
	else if (strncasecmp(text, "Content-Length:", 15) == 0)
	{
		text += 15;
		text += strspn(text, " \t");
		m_contentLength = atoi(text);
	}
	/* 处理HOST头部字段 */
	else if (strncasecmp(text, "Host:", 5) == 0)
	{
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	return NO_REQUEST;
}

// 判断请求体是否被完整读入
HttpConn::HTTP_CODE HttpConn::parseContent(char *text)
{
	// 读取到了完整的请求体
	if (m_readIdx >= (m_contentLength + m_checkedIdx))
	{
		text[m_contentLength] = '\0';
		/* POST请求中消息体的内容为用户输入的用户名和密码 */
		m_namePassword = text;
		return GET_REQUEST;
	}
	return NO_REQUEST;
}

/* 主状态机 */
HttpConn::HTTP_CODE HttpConn::processRead()
{
	LINE_STATUS lineStatus = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char *text = nullptr;

	while ((m_checkState == CHECK_STATE_CONTENT && lineStatus == LINE_OK) || (lineStatus = parseLine()) == LINE_OK)
	{
		text = getLine();
		// 将m_startLine移动到每一行的起始位置
		m_startLine = m_checkedIdx;
		//LOG_INFO("%s", text);

		switch (m_checkState)
		{
		case CHECK_STATE_REQUESTLINE:
		{
			ret = parseRequestLine(text);
			if (ret == BAD_REQUEST)
			{
				return BAD_REQUEST;
			}
			break;
		}
		case CHECK_STATE_HEADER:
		{
			ret = parseHeaders(text);
			if (ret == BAD_REQUEST)
			{
				return BAD_REQUEST;
			}
			else if (ret == GET_REQUEST)
			{
				return doRequest();
			}
			break;
		}
		case CHECK_STATE_CONTENT:
		{
			ret = parseContent(text);
			if (ret == GET_REQUEST)
			{
				return doRequest();
			}
			lineStatus = LINE_OPEN;
			break;
		}
		default:
		{
			return INTERNAL_ERROR;
		}
		}
	}
	return NO_REQUEST;
}
/* 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性。如果目标文件存在
 * 且对所有用户可读，则使用mmap将其映射到内存地址m_fileAddress处，并返回成功 */
HttpConn::HTTP_CODE HttpConn::doRequest()
{
	strcpy(m_realFile, m_docRoot);
	int len = strlen(m_docRoot);

	const char *p = strrchr(m_url, '/');

	/* 处理CGI */
	if (m_method == POST)
	{
		/* 提交表单时的url*/
		if (strncasecmp(p + 1, "log.cgi", 7) == 0)
		{
			CGI_UserLog();
		}
		else if (strncasecmp(p + 1, "regist.cgi", 10) == 0)
		{
			CGI_UserRegist();
		}
	}
	strncpy(m_realFile + len, m_url, FILENAME_LEN - len - 1);

	// 判断文件的属性和权限
	if (stat(m_realFile, &m_fileStat) < 0)
	{
		return NO_RESOURCE;
	}

	if (!(m_fileStat.st_mode & S_IROTH))
	{
		return FORBIDDEN_REQUEST;
	}

	if (S_ISDIR(m_fileStat.st_mode))
	{
		return BAD_REQUEST;
	}

	int fd = open(m_realFile, O_RDONLY);
	//映射文件
	m_fileAddress = (char *)mmap(0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}

/* 对内存映射区执行munmap操作 */
void HttpConn::unmap()
{
	if (m_fileAddress)
	{
		munmap(m_fileAddress, m_fileStat.st_size);
		m_fileAddress = nullptr;
	}
}

/* 写HTTP响应 */
bool HttpConn::writen()
{
	int temp = 0;
	// 没有东西可写
	if (m_bytesToSend == 0)
	{
		//printf("m_bytesToSend = %d\n", m_bytesToSend);
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		init();
		return true;
	}

	while (true)
	{
		// 写缓冲和mmap映射的文件缓冲
		temp = writev(m_sockfd, m_iv, m_ivCount);
		if (temp <= -1)
		{
			/* 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件 */
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				modfd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			unmap();
			return false;
		}

		// tcp写缓冲的限制，需要记录已写和未写的字符数
		m_bytesToSend -= temp;
		m_bytesHaveSend += temp;
		/* 解决大文件的传输问题 */
		if (m_bytesHaveSend >= m_iv[0].iov_len)
		{
			m_iv[0].iov_len = 0;
			m_iv[1].iov_base = m_fileAddress + (m_bytesHaveSend - m_writeIdx);
			m_iv[1].iov_len = m_bytesToSend;
		}
		else
		{
			m_iv[0].iov_base = m_writeBuf + m_bytesHaveSend;
			m_iv[0].iov_len = m_iv[0].iov_len - m_bytesHaveSend;
		}

		/* 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接 */
		if (m_bytesToSend <= 0)
		{
			unmap();
			modfd(m_epollfd, m_sockfd, EPOLLIN);
			if (m_linger)
			{
				init();
				return true;
			}
			else
			{
				return false;
			}
		}
	}
}

/* 往写缓冲区中写入待发送的数据 */
bool HttpConn::addResponse(const char *format, ...)
{
	if (m_writeIdx >= WRITE_BUFFER_SIZE)
	{
		return false;
	}
	va_list argList;
	va_start(argList, format);
	int len = vsnprintf(m_writeBuf + m_writeIdx, WRITE_BUFFER_SIZE - m_writeIdx, format, argList);
	if (len >= WRITE_BUFFER_SIZE - m_writeIdx)
	{
		va_end(argList);
		return false;
	}
	m_writeIdx += len;
	va_end(argList);

	//LOG_INFO("request: %s", m_writeBuf);
	return true;
}

bool HttpConn::addStatusLine(int status, const char *title)
{
	return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::addHeaders(int contentLength)
{
	addContentLength(contentLength);
	addLinger();
	addBlankLine();
	return true;
}

bool HttpConn::addContentLength(int contentLength)
{
	return addResponse("Content-Length: %d\r\n", contentLength);
}

bool HttpConn::addLinger()
{
	return addResponse("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::addBlankLine()
{
	return addResponse("%s", "\r\n");
}

bool HttpConn::addContent(const char *content)
{
	return addResponse("%s", content);
}
/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容 */
bool HttpConn::processWrite(HTTP_CODE ret)
{
	switch (ret)
	{
	case INTERNAL_ERROR:
	{
		addStatusLine(500, ERROR_500_TITLE);
		addHeaders(strlen(ERROR_500_FORM));
		if (!addContent(ERROR_500_FORM))
		{
			return false;
		}
		break;
	}
	case BAD_REQUEST:
	{
		addStatusLine(400, ERROR_400_TITLE);
		addHeaders(strlen(ERROR_400_FORM));
		if (!addContent(ERROR_400_FORM))
		{
			return false;
		}
		break;
	}
	case NO_RESOURCE:
	{
		addStatusLine(404, ERROR_404_TITLE);
		addHeaders(strlen(ERROR_404_FORM));
		if (!addContent(ERROR_404_FORM))
		{
			return false;
		}
		break;
	}
	case FORBIDDEN_REQUEST:
	{
		addStatusLine(403, ERROR_403_TITLE);
		addHeaders(strlen(ERROR_403_FORM));
		if (!addContent(ERROR_403_FORM))
		{
			return false;
		}
		break;
	}
	case FILE_REQUEST:
	{
		addStatusLine(200, OK_200_TITLE);
		if (m_fileStat.st_size != 0)
		{
			//printf("m_fileStat.st_size = %d\n", m_fileStat.st_size);
			addHeaders(m_fileStat.st_size);
			m_iv[0].iov_base = m_writeBuf;
			m_iv[0].iov_len = m_writeIdx;
			m_iv[1].iov_base = m_fileAddress;
			m_iv[1].iov_len = m_fileStat.st_size;
			m_ivCount = 2;
			m_bytesToSend = m_writeIdx + m_fileStat.st_size;
			//printf("m_bytesToSend = %d\n", m_bytesToSend);
			return true;
		}
		else
		{
			const char *OK_STRING = "<html><body></body></html>";
			addHeaders(strlen(OK_STRING));
			if (!addContent(OK_STRING))
			{
				return false;
			}
		}
	}
	default:
	{
		return false;
	}
	}
	m_iv[0].iov_base = m_writeBuf;
	m_iv[0].iov_len = m_writeIdx;
	m_ivCount = 1;
	m_bytesToSend = m_writeIdx;
	//printf("m_bytesToSend = %d\n", m_bytesToSend);
	return true;
}

/* 由线程池中的工作线程调用，这是处理HTTP请求的入口函数 */
void HttpConn::process()
{
	HTTP_CODE readRet = processRead();
	if (readRet == NO_REQUEST)
	{
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}
	bool writeRet = processWrite(readRet);
	if (!writeRet)
	{
		closeConn();
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

void HttpConn::CGI_UserLog()
{
	/* 首先从请求体内容中解析出用户名和密码 */
	std::string name, password;
	getNameAndPwd(name, password);
	/* 登录成功*/
	if (m_users.find(name) != m_users.end() && m_users[name] == password)
	{
		strcpy(m_url, "/welcome.html");
	}
	else
	{
		strcpy(m_url, "/logError.html");
	}
}

void HttpConn::CGI_UserRegist()
{
	/* 首先从请求体内容中解析出用户名和密码 */
	std::string name, password;
	getNameAndPwd(name, password);
	/* 先检测数据库中是否有重名的 */
	/* 若没有重名的，直接插入 */
	if (m_users.find(name) == m_users.end())
	{
		char sql[200] = {0};
		sprintf(sql, "insert into webserver(user, passwd) values('%s', '%s')", name.c_str(), password.c_str());
		m_lock.lock();
		//printf("ptr.count = %d\n", sql_con.use_count());
		bool res = sql_con->update(sql);
		m_users[name] = password;
		m_lock.unlock();
		if (res)
		{
			strcpy(m_url, "/log.html");
		}
		else
		{
			strcpy(m_url, "/registError.html");
		}
	}
	else
	{
		strcpy(m_url, "/registError.html");
	}
}

bool HttpConn::getNameAndPwd(std::string &name, std::string &password)
{
	// user=123&password=123
	int i;
	for (i = 5; m_namePassword[i] != '&'; ++i)
	{
		name.push_back(m_namePassword[i]);
	}
	for (i = i + 10; i < m_namePassword.length(); ++i)
	{
		password.push_back(m_namePassword[i]);
	}
	return true;
}
