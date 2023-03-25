#define _CRT_SECURE_NO_WARNINGS 1

#include "CentralCache.h"
#include "PageCache.h"

// 静态成员变量不能够在.h文件中初始化，需要在.cpp中初始化，否则可能存在问题
CentralCache CentralCache::_sInstance;

// 获取一个非空的span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 1、查看当前的spanlist中是否有还有未分配对象的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		// 错误写法，要看的是span中自由链表是否还挂有对象
		//if (it != nullptr)
		//	return it;
		if (it->_freeList != nullptr)
			return it;
		it = it->_next;
	}

	// spanList中不存在非空的span，向Page Cache申请
	// 在向Page Cache申请过程中Thread Cache可能会还回来空间，所以此时可以把这个位置的桶锁解掉
	list._mtx.unlock();

	// 在向Page Cache申请时需要对Page Cache的大锁进行加锁
	PageCache::GetIntance()->_pageMtx.lock();
	Span* span = PageCache::GetIntance()->NewSpan(SizeClass::NumMovePage(size));
	// 将从Page Cache获取到的span的使用状态置为true
	span->_isUse = true;

	// 将size小对象大小保存至span中的_objSize
	span->_objSize = size;
	PageCache::GetIntance()->_pageMtx.unlock();

	// 计算span的大块内存的起始地址和大块内存的大小（字节数）
	// 如何找到一个span所管理的内存块呢？首先需要计算出该span的起始地址，
	// 我们可以用这个span的起始页号乘以一页的大小即可得到这个span的起始地址，
	// 然后用这个span的页数乘以一页的大小就可以得到这个span所管理的内存块的大小，
	// 用起始地址加上内存块的大小即可得到这块内存块的结束位置。

	// 对获取span进行切分，不需要加锁，因为这会其他线程访问不到这个span
	char* start = (char*)(span->_pageId << PAGE_SHIFT);	
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// 先切一块下来去做尾，方便尾插
	span->_freeList = start;
	start += size;
	// 调试后发现的错误写法，要知道start+=size后，tail=start的话，实际上span中的_freeList
	// 是无法链接起来的，因为一开始取不到start的头上4/8个字节了
	//void* tail = start;
	
	void* tail = span->_freeList;
	// 尾插，确保空间是连续的，还回来的时候也是连续的
	//int i = 1; // i作为测试用，i最后应该为1024
	while (start < end)
	{
		//i++;
		NextObj(tail) = start;
		tail = NextObj(tail);
		start += size;
	}
	NextObj(tail) = nullptr;
	// 将从Page Cache切好的空间的span头插会Central Cache

	// 切好span以后，需要把span挂到桶里面去的时候，再加锁
	list._mtx.lock();
	list.PushFront(span);
	return span;
}

// 从中心缓存中获取一定数量的对象给Thread Cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t bytes)
{
	// 在与Thread Cache相同位置的桶获取空间给Thread Cache
	size_t index = SizeClass::Index(bytes);

	// 在访问Central Cache前一定要对Central Cache中相应位置的桶锁进行加锁
	// 保证每一次只能够有一个Thread Cache线程去访问Central Cache中的这个桶
	// 需要注意的是我们不是对整个Central Cache进行加锁，而是Central Cache中的
	// 每一个桶都有一把锁，我们只需对这个桶中的锁加锁，这样就可以保证如果多个Thread Cache线程
	// 访问的不是Central Cache中的同一个桶就不需要进行阻塞等待，可以并发执行，提高内存池效率
	
	_spanList[index]._mtx.lock();

	// 获取一个非空的Span
	Span* span = GetOneSpan(_spanList[index], bytes);
	assert(span);
	assert(span->_freeList);

	// 从span中获取batchNum个对象
	// 如果不够batchNum个，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t actualNum = 1;
	size_t i = 0;
	// batchNum要获取的空间数，如获取三个空间，则走3-1步end指向第三个空间
	while (i < batchNum - 1 && NextObj(end) != nullptr) // 防止span中空间不够，end走到空
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	// 把空间切出去后，更新此时span中的_freeList自由链表头指针
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;
	// 分配给thread cache空间操作完成，进行解锁
	_spanList[index]._mtx.unlock();

	return actualNum;
}

// Thread Cache将自由链表中的一定数量的对象还给Central Cache中对应的span
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	// 访问Central Cache的桶时把对应的桶锁加上
	_spanList[index]._mtx.lock();

	// 将还回来的内存块头插进span中
	while (start)
	{
		void* next = NextObj(start);
		// 找到这些还回来的内存块对应的span
		Span* span = PageCache::GetIntance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		// 每还回来一个对象空间，span中_useCount计数就减减
		span->_useCount--;
		// _useCount计数为0就说明从Central Cache切分出去的小块内存块都还回来了
		// 此时就可以把这个span里空间还给Page Cache了
		if (span->_useCount == 0)
		{
			// 还回去时清理一下
			// 在带头双向循环的span中Erase掉这个还回去的span
			_spanList[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;
			
			// 还空间之前先把Central Cache中的桶解掉
			// 释放span给page cache时，使用page cache的锁就可以了
			_spanList[index]._mtx.unlock();

			// 访问Page Cache前把Page Cache的大锁进行加锁
			PageCache::GetIntance()->_pageMtx.lock(); 
			PageCache::GetIntance()->ReleaseSpanToPageCache(span);
			PageCache::GetIntance()->_pageMtx.unlock();

			// 把span还回Page Cache后，回来后继续加入到锁竞争
			_spanList[index]._mtx.lock();
		}
		start = next;
	}

	_spanList[index]._mtx.unlock();
}