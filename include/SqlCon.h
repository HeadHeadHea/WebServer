#ifndef __SQL__CON__
#define __SQL__CON__


#include"Locker.h"
#include <mysql.h>
#include<string>
#include"Log.h"

class SqlCon{
public:
    //1.初始化连接
    SqlCon();
    //2.销毁连接
    ~SqlCon();
    //3.连接数据库
    bool connect(const char* hostname, const char* username = "root", const char* passwd = "123456", 
    const char* dbname = "test", const unsigned short port = 3306);

    bool connect(const std::string hostname, const std::string username = "root", const std::string passwd = "123456",
    const std::string dbname = "test", const unsigned short port = 3306);
    //4.更新数据库
    bool update(const char* sql);
    bool update(const std::string sql);
    //5.查询数据库
    bool query(const char* sql);
    bool query(const std::string sql);
    //6.返回结果集中的一行数据
    bool getRow();
    //7.返回一行数据中指定的字段值
    std::string getField(unsigned index); 
    //8.设置事务
    bool setTransaction();
    //9.提交事务
    bool commit();
    //10.事务回滚
    bool rollBack();
    //11.返回结果集中字段的数量
    unsigned getNumFields();
    //12.刷新起始空闲时间点
    void flushFreeTime();
    //13.计算连接存活总时长(返回毫秒)
    long long getFreeTime();
private:
    //释放结果集
    void freeRes();

    MYSQL* m_con = NULL;
    MYSQL_RES* m_result = NULL;
    MYSQL_ROW m_row = NULL;
    //绝对时钟
    struct timeval m_free_time;
};

#endif