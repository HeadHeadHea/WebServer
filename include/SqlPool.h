#ifndef __SQL__POOL__H
#define __SQL__POOL__H


#include"SqlCon.h"
#include<memory>
#include<new>
#include<list>
#include<fstream>
#include<sstream>
#include<vector>
#include<iostream>


//单例模式
class SqlPool{
public:
    //禁止拷贝
    SqlPool(const SqlPool&) = delete;
    SqlPool& operator= (const SqlPool&) = delete;
    ~SqlPool();

    //获取实例
    static std::shared_ptr<SqlPool> getInstance();

    //获取和回收连接，使用智能指针进行管理
    std::shared_ptr<SqlCon> getRecyCon();

private:
    SqlPool();
    //读取配置文件加载连接池所需参数
    bool loading();
    //线程函数
    static void* produceCon(void*);
    static void* destroyCon(void*);
    //创建连接函数
    void addCon();
    //释放连接函数
    void freeCon();

    static SqlPool* m_pool_ptr;

    //数据库连接所需参数
    std::string m_hostname;
    std::string m_username;
    std::string m_passwd;
    std::string m_dbname;
    unsigned short m_port;

    //连接池参数
    int m_maxsize = 0;
    int m_minsize = 0;

    //下面两个时间的单位是毫秒
    //线程等待最大超时时间
    long m_timeout = 0;
    //空闲连接最大等待时长
    long m_waittime = 0;

    //访问list所需的锁和条件变量
    std::list<SqlCon*> m_list;
    Locker m_pool_mutex;
    Cond m_pool_cond;

    //此标志位用于停止连接池
    bool m_shutdown = false;
    //管理生产和销毁线程的数组
    std::vector<pthread_t> m_tid_arr;
};

#endif