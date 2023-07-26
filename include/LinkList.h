#ifndef __LINK__LIST__
#define __LINK__LIST__


#include<sys/socket.h>
#include<arpa/inet.h>
#include<string.h>

//HttpConn关联的定时器
class Timer{
public:
    using cb = void(*)(void*);

    Timer():expire(-1), cb_func(nullptr), client(nullptr), prev(nullptr), next(nullptr){}
    //绝对时间
    uint32_t expire;
    cb cb_func; 
    void* client;
    Timer* prev;
    Timer* next; 
};

//双向循环链表
class LinkList{
public:
    LinkList();
    ~LinkList();
    //头插法
    void addTimerFront(Timer* timer);
    //尾插法
    void addTimerBack(Timer* timer);
    //链表是否为空
    bool isEmpty();
    //链表1中的结点放入链表2
    void splice(LinkList& list);
    //获取头节点
    Timer* getHead() const;
private:
    Timer* head;
};

//移除结点
void removeTimer(Timer* timer);

#endif