#pragma once

#include "Common.h"

class ThreadCache
{
public:
	// 申请和释放内存对象
	void* Allocate(size_t size);

	// 需要通过size找到对应位置的桶进行释放
	void Deallocate(void* ptr, size_t size);

	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象导致Thread Cache中自由链表过长，回收内存到中心缓存
	void ListTooLong(FreeList& list, size_t size);

private:
	// 自由链表对象, 一共有208个桶
	FreeList _freeLists[N_FREE_LIST];
};

// 使用关键字_declspec(thread)将变量声明为TLS变量(Thread Local Storage)
// TLS 变量是一种特殊类型的变量，每个线程都有自己独立的存储空间，
// 不同线程之间互相独立，可以同时访问不同的变量副本，互不干扰,避免多线程编程中的线程安全问题。
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;