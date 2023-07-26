#ifndef __TIMER__WHEEL__
#define __TIMER__WHEEL__

#include"LinkList.h"

#define TVR_BITS 8
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_BITS 6
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_MASK (TVR_SIZE - 1)
#define TVN_MASK (TVN_SIZE - 1)

//第一圈指针的位置
#define FIRST_INDEX(v) (v & TVR_MASK)
//其它圈指针的位置
#define NTH_INDEX(v, n) ((v >> (TVR_BITS + n * TVN_BITS)) & TVN_MASK)

class TimerWheel{
public:
    //初始化时间轮
    TimerWheel(uint16_t interval, uint32_t currtime);
    //初始化时间结点
    void wheelNodeInit(Timer* timer, Timer::cb cb_func, void* client);
    //添加时间结点
    void wheelNodeAdd(Timer* timer);
    //设置定时器时间并添加
    void setTimer(Timer* timer, uint32_t ticks);
    //删除结点
    void wheelNodeDel(Timer* timer);
    //降级函数
    void wheelCascade(LinkList* tv, int idx);
    //tick函数
    void wheelTick();
    //更新时间轮
    void wheelUpdate(uint64_t currtime);
private:
    LinkList tvroot[TVN_SIZE];
    LinkList tv[4][TVR_SIZE];
    //上一次运行时间
    uint64_t lasttime;
    //当前tick
    uint32_t currtick;
    //每个时间点的间隔
    uint16_t interval;
    //剩余时间
    uint16_t remainder;
};

#endif