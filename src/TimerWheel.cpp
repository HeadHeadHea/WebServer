#include"../include/TimerWheel.h"

TimerWheel::TimerWheel(uint16_t interval, uint32_t currtime){
    this->interval = interval;
    this->lasttime = currtime;
    this->currtick = 0;
}

void TimerWheel::wheelNodeInit(Timer* timer, Timer::cb cb_func, void* client){
    timer->cb_func = cb_func;
    timer->client = client;
}

void TimerWheel::wheelNodeAdd(Timer* timer){
    uint32_t expire = timer->expire;
    uint32_t idx = expire - currtick;
    LinkList* head;
    if(idx < TVR_SIZE){
        head = tvroot + FIRST_INDEX(expire);
    }
    else{
        int i = 0;
        uint64_t sz = 0;
        for(i = 0; i < 4; ++i){
            //求第二个轮的最大范围
            sz = (1ULL) << (TVR_BITS + (i + 1) * TVN_BITS);
            if(idx < sz){
                head = tv[i] + NTH_INDEX(expire, i);
                break;
            }
        }
    }
    head->addTimerBack(timer);
}

void TimerWheel::setTimer(Timer* timer, uint32_t ticks){
    timer->expire = currtick + (ticks > 0 ? ticks : 1);
    wheelNodeAdd(timer);
}


void TimerWheel::wheelNodeDel(Timer* timer){
    removeTimer(timer);
}

void TimerWheel::wheelCascade(LinkList* tv, int idx){
    LinkList list2;
    LinkList* list1 = tv + idx;
    list1->splice(list2);

    while(!list2.isEmpty()){
        Timer* node = list2.getHead()->next;
        removeTimer(node);
        wheelNodeAdd(node);
    }
}

void TimerWheel::wheelTick(){
    ++currtick;
    int index = currtick & TVR_MASK;
    //第一个轮转了一圈
    if(index == 0){
        int i = 0;
        int idx;
        do{
            idx = NTH_INDEX(currtick, i);
            wheelCascade(tv[i], idx);
            ++i;
            //idx == 0代表第i个轮转了一圈，再次降级
        }while(idx == 0 && i < 4);
    }

    //定时器执行回调函数
    LinkList list2;
    LinkList* list1 = tvroot + index;
    list1->splice(list2);
    while(!list2.isEmpty()){
        Timer* node = list2.getHead()->next;
        if(node->cb_func){
            node->cb_func(node->client);
        }
        wheelNodeDel(node);
    }
}

void TimerWheel::wheelUpdate(uint64_t currtime){
    if(currtime > lasttime){
        int diff = currtime - lasttime + remainder;
        int intv = interval;
        lasttime = currtime;
        while(diff >= interval){
            diff -= interval;
            wheelTick();
        }
        remainder = diff;
    }
}