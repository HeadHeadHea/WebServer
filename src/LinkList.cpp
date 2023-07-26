#include"../include/LinkList.h"

LinkList::LinkList(){
    head = new Timer;
    head->prev = head;
    head->next = head;
}

LinkList::~LinkList(){
    Timer* p = head->next;
    while(p != head){
        head = p->next;
        delete p;
        p = head;
    }
    delete p;
}

void LinkList::addTimerFront(Timer* timer){
    timer->next = head->next;
    timer->prev = head;
    head->next->prev = timer;
    head->next = timer;
}

void LinkList::addTimerBack(Timer* timer){
    timer->next = head;
    timer->prev = head->prev;
    head->prev->next = timer;
    head->prev = timer;
}

bool LinkList::isEmpty(){
    return head->next == head;
}

void removeTimer(Timer* timer){
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    timer->next = timer;
    timer->prev = timer;
}

Timer* LinkList::getHead() const{
    return head;
}

void LinkList::splice(LinkList& list){
    if(!isEmpty()){
        Timer* first = head->next;
        Timer* end = head->prev;
        Timer* at = list.getHead();
        first->prev = at;
        end->next = at->next;
        at->next->prev = end;
        at->next = first;
        head->next = head;
        head->prev = head;
    }
}