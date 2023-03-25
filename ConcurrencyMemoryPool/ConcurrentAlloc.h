#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"

static void* ConcurrentAlloc(size_t size)
{
	// 大于256kb的内存申请直接去找PageCache，大于128页的内存申请找堆
	if (size > MAX_BYTES)
	{
		// 计算出对齐后需要申请的页数
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kPage = alignSize >> PAGE_SHIFT;

		// 向page cache申请kPage页的span
		// 访问PageCache都需要加大锁
		PageCache::GetIntance()->_pageMtx.lock();
		Span* span = PageCache::GetIntance()->NewSpan(kPage);
		// 申请大于256kb不会进入到CentralCache中申请，所以其_objszie就不会被设置
		// 需要在此处设置
		span->_objSize = size;
		PageCache::GetIntance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// 通过TLS，每个线程无锁的获取自己专属的ThreadCache对象
		if (pTLSThreadCache == nullptr)
		{
			// 每个线程都有自己的一个Thread Cache对象, Thread Cache对象又是一个具有208个桶
			// 每个桶上又挂着一个自由链表的哈希桶
			//pTLSThreadCache = new ThreadCache;
			static std::mutex tcMtx;
			static ObjectPool<ThreadCache> tcPool;
			tcMtx.lock();
			pTLSThreadCache = tcPool.New();
			tcMtx.unlock();
		}
		// 通过这个指针去调用指向对象的Allocate函数获取空间
		//cout << std::this_thread::get_id() << " : " << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}
	}

//static void ConcurrentFree(void* ptr, size_t size)
//{
//	if (size > MAX_BYTES)
//	{
//		// 计算这个ptr映射的是哪一个span
//		Span* span = PageCache::GetIntance()->MapObjectToSpan(ptr);
//
//		// 将这个大于256kb的空间直接还给堆
//		PageCache::GetIntance()->_pageMtx.lock();
//		PageCache::GetIntance()->ReleaseSpanToPageCache(span);
//		PageCache::GetIntance()->_pageMtx.unlock();
//	}
//	else
//	{
//		assert(pTLSThreadCache);
//		pTLSThreadCache->Deallocate(ptr, size);
//	}
//}

// 优化free，使其释放空间时不需要传入待释放的对象空间大小
static void ConcurrentFree(void* ptr)
{
	// 计算这个ptr映射的是哪一个span
	// central cache释放内存给PageCache时会访问这个映射关系，
	// 而在调用ConcurrentFree函数释放内存时也会访问这个映射关系，那么就存在线程安全的问题
	// 那么就应该在这个映射关系函数内部加上锁，因为函数是在PageCache中，需加上PAgeCache的锁
	Span* span = PageCache::GetIntance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;
	if (size > MAX_BYTES)
	{
		// 将这个大于256kb的空间直接还给堆
		PageCache::GetIntance()->_pageMtx.lock();
		PageCache::GetIntance()->ReleaseSpanToPageCache(span);
		PageCache::GetIntance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}