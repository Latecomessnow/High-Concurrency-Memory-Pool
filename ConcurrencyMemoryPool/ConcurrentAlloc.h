#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"

static void* ConcurrentAlloc(size_t size)
{
	// ����256kb���ڴ�����ֱ��ȥ��PageCache������128ҳ���ڴ������Ҷ�
	if (size > MAX_BYTES)
	{
		// ������������Ҫ�����ҳ��
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kPage = alignSize >> PAGE_SHIFT;

		// ��page cache����kPageҳ��span
		// ����PageCache����Ҫ�Ӵ���
		PageCache::GetIntance()->_pageMtx.lock();
		Span* span = PageCache::GetIntance()->NewSpan(kPage);
		// �������256kb������뵽CentralCache�����룬������_objszie�Ͳ��ᱻ����
		// ��Ҫ�ڴ˴�����
		span->_objSize = size;
		PageCache::GetIntance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// ͨ��TLS��ÿ���߳������Ļ�ȡ�Լ�ר����ThreadCache����
		if (pTLSThreadCache == nullptr)
		{
			// ÿ���̶߳����Լ���һ��Thread Cache����, Thread Cache��������һ������208��Ͱ
			// ÿ��Ͱ���ֹ���һ����������Ĺ�ϣͰ
			//pTLSThreadCache = new ThreadCache;
			static std::mutex tcMtx;
			static ObjectPool<ThreadCache> tcPool;
			tcMtx.lock();
			pTLSThreadCache = tcPool.New();
			tcMtx.unlock();
		}
		// ͨ�����ָ��ȥ����ָ������Allocate������ȡ�ռ�
		//cout << std::this_thread::get_id() << " : " << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}
	}

//static void ConcurrentFree(void* ptr, size_t size)
//{
//	if (size > MAX_BYTES)
//	{
//		// �������ptrӳ�������һ��span
//		Span* span = PageCache::GetIntance()->MapObjectToSpan(ptr);
//
//		// ���������256kb�Ŀռ�ֱ�ӻ�����
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

// �Ż�free��ʹ���ͷſռ�ʱ����Ҫ������ͷŵĶ���ռ��С
static void ConcurrentFree(void* ptr)
{
	// �������ptrӳ�������һ��span
	// central cache�ͷ��ڴ��PageCacheʱ��������ӳ���ϵ��
	// ���ڵ���ConcurrentFree�����ͷ��ڴ�ʱҲ��������ӳ���ϵ����ô�ʹ����̰߳�ȫ������
	// ��ô��Ӧ�������ӳ���ϵ�����ڲ�����������Ϊ��������PageCache�У������PAgeCache����
	Span* span = PageCache::GetIntance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;
	if (size > MAX_BYTES)
	{
		// ���������256kb�Ŀռ�ֱ�ӻ�����
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