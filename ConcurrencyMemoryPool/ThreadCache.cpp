#define _CRT_SECURE_NO_WARNINGS 1

#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::Allocate(size_t bytes)
{
	assert(bytes <= MAX_BYTES);
	size_t alignBytes = SizeClass::RoundUp(bytes);
	size_t index = SizeClass::Index(bytes);
	// 如果在该索引位置不为空，则在这里拿空间
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	// 如果该位置自由链表为空，则从CentralCache中获取空间
	else
	{
		return FetchFromCentralCache(index, alignBytes);
	}
}

// 释放对象导致Thread Cache中自由链表过长，回收内存到中心缓存
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	// start, end做PopRange的输出型参数
	void* start = nullptr;
	void* end = nullptr;
	// 从list中取出一次批量个数的对象
	list.PopRange(start, end, list.GetMaxSize());

	// 将取出的对象还回到Central Cache对应的span中
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}

// 传入被释放对象的字节数是为了根据字节数找到对应的桶进行Push归还空间
void ThreadCache::Deallocate(void* ptr, size_t bytes)
{
	assert(ptr);
	assert(bytes <= MAX_BYTES);
	// 找到对象字节数映射的ThreadCache的桶，调用Push函数将对象空间归还给自由链表
	size_t index = SizeClass::Index(bytes);
	_freeLists[index].Push(ptr);

	// 当自由链表长度大于一次批量申请的对象个数时就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].GetMaxSize())
	{
		ListTooLong(_freeLists[index], bytes);
	}
}

// 传入index让Central Cache中也到相应的桶位置去拿空间
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法
	// 1、最开始不会一次向central cache一次批量要太多，因为要太多了可能用不完
	// 2、如果你不要这个size大小内存需求，那么batchNum就会不断增长，直到上限
	// 3、size越大，一次向central cache要的batchNum就越小
	// 4、size越小，一次向central cache要的batchNum就越大

	// Central Cache给thread cache分配的字节空间数
	// windows下的min宏会和算法库里的min冲突
	size_t batchNum = min(_freeLists[index].GetMaxSize(), SizeClass::NumMoveSize(size));
	if (batchNum == _freeLists[index].GetMaxSize())
	{
		_freeLists[index].GetMaxSize() += 1;
	}

	// strat end做输出型参数，获取从Central Cache中拿到的切分好的自由链表的头尾指针
	void* start = nullptr;
	void* end = nullptr;

	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	// 至少要给一个, 所以要大于0
	assert(actualNum > 0);

	if (actualNum == 1)
	{
		// 申请到对象的个数是一个，则直接将这一个对象返回给申请空间的线程即可
		assert(start == end);
		return start;
	}
	else
	{
		// 申请到对象的个数是多个，还需要将剩下的对象挂到thread cache中对应的哈希桶中
		// 第一个start那块空间就返回给申请空间的线程
		// 此处push进自由链表的对象的个数需要减一，因为有一个对象返给了申请空间的线程
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}