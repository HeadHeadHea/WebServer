#include"../include/SqlCon.h"


SqlCon::SqlCon(){
    m_con = mysql_init(NULL);
    if(m_con == NULL){
        //printSqlErr("mysql_init error",m_con);
        LOG_ERROR("SqlCon::mysql_init error: %s", mysql_error(m_con));
    }
    //设置数据库字符集
    mysql_set_character_set(m_con, "utf8mb4");
}

SqlCon::~SqlCon(){
    if(m_con == NULL){
        return;
    }
    mysql_close(m_con);
    freeRes();
}

bool SqlCon::connect(const char* hostname, const char* username, const char* passwd, const char* dbname, const unsigned short port){
    m_con = mysql_real_connect(m_con, hostname, username, passwd, dbname, port, NULL, 0);

    if(m_con == NULL){
        //printSqlErr("mysql_real_connect error",m_con);
        LOG_ERROR("SqlCon::mysql_connect error: %s", mysql_error(m_con));
        return false;
    }
    return true;
}

bool SqlCon::connect(const std::string hostname, const std::string username, const std::string passwd, 
const std::string dbname, const unsigned short port)
{
    const char* hname = hostname.c_str();
    const char* uname = username.c_str();
    const char* pwd = passwd.c_str();
    const char* databasename = dbname.c_str();
    return connect(hname, uname, pwd, databasename, port);
}

bool SqlCon::update(const char* sql){
    int ret = mysql_query(m_con, sql);
    if(ret != 0){
        //printSqlErr("mysql_update error",m_con);
        LOG_ERROR("SqlCon::mysql_update error: %s", mysql_error(m_con));
        return false;
    }
    return true;
}

bool SqlCon::update(const std::string sql){
    const char* sqlstr = sql.c_str();
    return update(sqlstr);
}

bool SqlCon::query(const char* sql){
    //释放上一次查询的结果集
    freeRes();
    int ret = mysql_query(m_con, sql);
    if(ret != 0){
        //printSqlErr("mysql_query error",m_con);
        LOG_ERROR("SqlCon::mysql_query error: %s", mysql_error(m_con));
        return false;
    }
    //保存结果集
    m_result = mysql_store_result(m_con);
    if(m_result == NULL){
        //printSqlErr("mysql_store_result error",m_con);
        LOG_ERROR("SqlCon::mysql_store error: %s", mysql_error(m_con));
        return false;
    }
    return true;
}

bool SqlCon::query(const std::string sql){
    const char* sqlstr = sql.c_str();
    return query(sqlstr);
}

bool SqlCon::getRow(){
    if(m_result == NULL){
        return false;
    }
    m_row = mysql_fetch_row(m_result);
    if(m_row == NULL){
        //printSqlErr("mysql_fetch_row",m_con);
        LOG_ERROR("SqlCon::mysql_fetch_row error: %s", mysql_error(m_con));
        return false;
    }
    return true;
}

std::string SqlCon::getField(unsigned index){
    unsigned fields_num = mysql_num_fields(m_result);
    if(index >= fields_num){
        return std::string();
    }
    const char* val = m_row[index];
    //获取字段长度
    unsigned long len = mysql_fetch_lengths(m_result)[index];
    return std::string(val, len);
}

bool SqlCon::setTransaction(){
    return mysql_autocommit(m_con, false);
}

bool SqlCon::commit(){
    return mysql_commit(m_con);
}

bool SqlCon::rollBack(){
    return mysql_rollback(m_con);
}

void SqlCon::freeRes(){
    if(m_result != NULL){
        mysql_free_result(m_result);
        m_result = NULL;
    }
}

unsigned SqlCon::getNumFields(){
    return mysql_num_fields(m_result);
}

void SqlCon::flushFreeTime(){
    gettimeofday(&m_free_time, NULL);
}

long long SqlCon::getFreeTime(){
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - m_free_time.tv_sec)*1000 + (now.tv_usec - m_free_time.tv_usec)/1000;
}
