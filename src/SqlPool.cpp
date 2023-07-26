#include "../include/SqlPool.h"

SqlPool *SqlPool::m_pool_ptr = nullptr;

void SqlPool::freeCon()
{
    m_shutdown = true;

    char *status = nullptr;

    // 回收线程
    for (pthread_t e : m_tid_arr)
    {
        int ret = pthread_join(e, (void **)&status);
        if (ret != 0)
        {
            // printSysErr("pthread_join error", ret);
            LOG_ERROR("SqlPool::pthread_join error: %s", strerror(ret));
        }
        // 打印回收线程的状态
        printf("SqlPool::pthread_join success: %lu status: %s\n", e, status);
    }

    // 销毁连接
    for (auto iter = m_list.begin(); iter != m_list.end(); ++iter)
    {
        delete *iter;
    }
    printf("SqlPool::SqlPool exit success\n");
}

std::shared_ptr<SqlPool> SqlPool::getInstance()
{
    if (m_pool_ptr == nullptr)
    {
        // 分配内存空间失败
        m_pool_ptr = new (std::nothrow) SqlPool;
        if (m_pool_ptr == nullptr)
        {
            // printErr("get_instance error");
            LOG_ERROR("SqlPool::new error");
            throw std::exception();
        }
    }
    // 返回智能指针
    return std::shared_ptr<SqlPool>(m_pool_ptr);
}

bool SqlPool::loading()
{
    std::ifstream read;
    std::vector<std::string> v;
    // 读取mysql_pool配置文件
    read.open("../mysql_conf.txt");
    if (!read.is_open())
    {
        // printErr("loading error");
        LOG_ERROR("SqlPool::open error");
        return false;
    }
    std::string lines;
    while (std::getline(read, lines))
    {
        std::istringstream info(lines);
        std::string str1, str2;
        info >> str1;
        info >> str2;
        v.push_back(str2);
    }
    m_hostname = v[0];
    m_username = v[1];
    m_passwd = v[2];
    m_dbname = v[3];
    m_port = std::stoi(v[4]);
    m_maxsize = std::stoi(v[5]);
    m_minsize = std::stoi(v[6]);
    m_timeout = std::stol(v[7]);
    m_waittime = std::stol(v[8]);
    read.close();
    return true;
}

SqlPool::~SqlPool()
{
    freeCon();
}

void SqlPool::addCon()
{
    SqlCon *con = new (std::nothrow) SqlCon;

    // 添加失败直接返回
    if (con == nullptr)
    {
        return;
    }

    // 连接失败
    if (!con->connect(m_hostname, m_username, m_passwd, m_dbname, m_port))
    {
        delete con;
        return;
    }

    con->flushFreeTime();
    // 添加一个连接
    m_pool_mutex.lock();
    m_list.push_back(con);
    m_pool_cond.signal();
    m_pool_mutex.unlock();
}

SqlPool::SqlPool()
{
    loading();

    // 创建连接
    for (int i = 0; i < m_maxsize; ++i)
    {
        addCon();
    }

    pthread_t tid = 0;
    int ret = pthread_create(&tid, NULL, produceCon, (void *)this);
    if (ret != 0)
    {
        freeCon();
        // printSysErr("pthread_create error", ret);
        LOG_ERROR("SqlPool::pthread_create error: %s", strerror(ret));
        exit(1);
    }
    m_tid_arr.push_back(tid);

    ret = pthread_create(&tid, NULL, destroyCon, (void *)this);
    if (ret != 0)
    {
        freeCon();
        // printSysErr("pthread_create error", ret);
        LOG_ERROR("SqlPool::pthread_create error: %s", strerror(ret));
        exit(1);
    }
    m_tid_arr.push_back(tid);
}

void *SqlPool::produceCon(void *arg)
{
    SqlPool *m_pool = (SqlPool *)arg;
    while (!m_pool->m_shutdown)
    {
        m_pool->m_pool_mutex.lock();

        // 池中连接数量超过最大数量
        while (!m_pool->m_shutdown && m_pool->m_list.size() > m_pool->m_maxsize)
        {
            m_pool->m_pool_cond.timedwait(&m_pool->m_pool_mutex.getMutex(), 2, 0);
        }

        m_pool->m_pool_mutex.unlock();
        m_pool->addCon();
    }
    const char *str = "producer thread exit success";
    return (void *)str;
}

void *SqlPool::destroyCon(void *arg)
{
    SqlPool *m_pool = (SqlPool *)arg;
    while (!m_pool->m_shutdown)
    {
        m_pool->m_pool_mutex.lock();
        // 连接池中连接数量小于最小数量
        while (!m_pool->m_shutdown && m_pool->m_list.size() < m_pool->m_minsize)
        {
            m_pool->m_pool_cond.timedwait(&m_pool->m_pool_mutex.getMutex(), 2, 0);
        }

        SqlCon *tmp = m_pool->m_list.front();
        // 此连接超过最大空闲时长
        if (tmp->getFreeTime() > m_pool->m_waittime)
        {
            delete tmp;
            m_pool->m_list.pop_front();
        }
        m_pool->m_pool_cond.signal();
        m_pool->m_pool_mutex.unlock();
    }
    const char *str = "destroy thread exit success";
    return (void *)str;
}

// 调用析构函数后应确保其它线程不再访问该函数，否则可能出现不可预知的错误
std::shared_ptr<SqlCon> SqlPool::getRecyCon()
{
    long tv_sec = m_timeout / 1000;
    long tv_nsec = (m_timeout % 1000) * 1000000;
    m_pool_mutex.lock();
    while (m_list.empty())
    {
        // 阻塞timeout时间
        m_pool_cond.timedwait(&m_pool_mutex.getMutex(), tv_sec, tv_nsec);
    }

    // 传递删除器
    auto recy = [this](SqlCon *con)
    {
        if (con)
        {
            this->m_pool_mutex.lock();
            this->m_list.push_back(con);
            this->m_pool_cond.signal();
            this->m_pool_mutex.unlock();
            con->flushFreeTime();
        }
    };

    std::shared_ptr<SqlCon> con_ptr(m_list.front(), recy);
    m_list.pop_front();
    m_pool_cond.signal();
    m_pool_mutex.unlock();
    return con_ptr;
}
