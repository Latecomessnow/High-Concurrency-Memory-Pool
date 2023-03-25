#define _CRT_SECURE_NO_WARNINGS 1

#include "CentralCache.h"
#include "PageCache.h"

// ��̬��Ա�������ܹ���.h�ļ��г�ʼ������Ҫ��.cpp�г�ʼ����������ܴ�������
CentralCache CentralCache::_sInstance;

// ��ȡһ���ǿյ�span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 1���鿴��ǰ��spanlist���Ƿ��л���δ��������span
	Span* it = list.Begin();
	while (it != list.End())
	{
		// ����д����Ҫ������span�����������Ƿ񻹹��ж���
		//if (it != nullptr)
		//	return it;
		if (it->_freeList != nullptr)
			return it;
		it = it->_next;
	}

	// spanList�в����ڷǿյ�span����Page Cache����
	// ����Page Cache���������Thread Cache���ܻỹ�����ռ䣬���Դ�ʱ���԰����λ�õ�Ͱ�����
	list._mtx.unlock();

	// ����Page Cache����ʱ��Ҫ��Page Cache�Ĵ������м���
	PageCache::GetIntance()->_pageMtx.lock();
	Span* span = PageCache::GetIntance()->NewSpan(SizeClass::NumMovePage(size));
	// ����Page Cache��ȡ����span��ʹ��״̬��Ϊtrue
	span->_isUse = true;

	// ��sizeС�����С������span�е�_objSize
	span->_objSize = size;
	PageCache::GetIntance()->_pageMtx.unlock();

	// ����span�Ĵ���ڴ����ʼ��ַ�ʹ���ڴ�Ĵ�С���ֽ�����
	// ����ҵ�һ��span��������ڴ���أ�������Ҫ�������span����ʼ��ַ��
	// ���ǿ��������span����ʼҳ�ų���һҳ�Ĵ�С���ɵõ����span����ʼ��ַ��
	// Ȼ�������span��ҳ������һҳ�Ĵ�С�Ϳ��Եõ����span��������ڴ��Ĵ�С��
	// ����ʼ��ַ�����ڴ��Ĵ�С���ɵõ�����ڴ��Ľ���λ�á�

	// �Ի�ȡspan�����з֣�����Ҫ��������Ϊ��������̷߳��ʲ������span
	char* start = (char*)(span->_pageId << PAGE_SHIFT);	
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// ����һ������ȥ��β������β��
	span->_freeList = start;
	start += size;
	// ���Ժ��ֵĴ���д����Ҫ֪��start+=size��tail=start�Ļ���ʵ����span�е�_freeList
	// ���޷����������ģ���Ϊһ��ʼȡ����start��ͷ��4/8���ֽ���
	//void* tail = start;
	
	void* tail = span->_freeList;
	// β�壬ȷ���ռ��������ģ���������ʱ��Ҳ��������
	//int i = 1; // i��Ϊ�����ã�i���Ӧ��Ϊ1024
	while (start < end)
	{
		//i++;
		NextObj(tail) = start;
		tail = NextObj(tail);
		start += size;
	}
	NextObj(tail) = nullptr;
	// ����Page Cache�кõĿռ��spanͷ���Central Cache

	// �к�span�Ժ���Ҫ��span�ҵ�Ͱ����ȥ��ʱ���ټ���
	list._mtx.lock();
	list.PushFront(span);
	return span;
}

// �����Ļ����л�ȡһ�������Ķ����Thread Cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t bytes)
{
	// ����Thread Cache��ͬλ�õ�Ͱ��ȡ�ռ��Thread Cache
	size_t index = SizeClass::Index(bytes);

	// �ڷ���Central Cacheǰһ��Ҫ��Central Cache����Ӧλ�õ�Ͱ�����м���
	// ��֤ÿһ��ֻ�ܹ���һ��Thread Cache�߳�ȥ����Central Cache�е����Ͱ
	// ��Ҫע��������ǲ��Ƕ�����Central Cache���м���������Central Cache�е�
	// ÿһ��Ͱ����һ����������ֻ������Ͱ�е��������������Ϳ��Ա�֤������Thread Cache�߳�
	// ���ʵĲ���Central Cache�е�ͬһ��Ͱ�Ͳ���Ҫ���������ȴ������Բ���ִ�У�����ڴ��Ч��
	
	_spanList[index]._mtx.lock();

	// ��ȡһ���ǿյ�Span
	Span* span = GetOneSpan(_spanList[index], bytes);
	assert(span);
	assert(span->_freeList);

	// ��span�л�ȡbatchNum������
	// �������batchNum�����ж����ö���
	start = span->_freeList;
	end = start;
	size_t actualNum = 1;
	size_t i = 0;
	// batchNumҪ��ȡ�Ŀռ��������ȡ�����ռ䣬����3-1��endָ��������ռ�
	while (i < batchNum - 1 && NextObj(end) != nullptr) // ��ֹspan�пռ䲻����end�ߵ���
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	// �ѿռ��г�ȥ�󣬸��´�ʱspan�е�_freeList��������ͷָ��
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;
	// �����thread cache�ռ������ɣ����н���
	_spanList[index]._mtx.unlock();

	return actualNum;
}

// Thread Cache�����������е�һ�������Ķ��󻹸�Central Cache�ж�Ӧ��span
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	// ����Central Cache��Ͱʱ�Ѷ�Ӧ��Ͱ������
	_spanList[index]._mtx.lock();

	// �����������ڴ��ͷ���span��
	while (start)
	{
		void* next = NextObj(start);
		// �ҵ���Щ���������ڴ���Ӧ��span
		Span* span = PageCache::GetIntance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		// ÿ������һ������ռ䣬span��_useCount�����ͼ���
		span->_useCount--;
		// _useCount����Ϊ0��˵����Central Cache�зֳ�ȥ��С���ڴ�鶼��������
		// ��ʱ�Ϳ��԰����span��ռ仹��Page Cache��
		if (span->_useCount == 0)
		{
			// ����ȥʱ����һ��
			// �ڴ�ͷ˫��ѭ����span��Erase���������ȥ��span
			_spanList[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;
			
			// ���ռ�֮ǰ�Ȱ�Central Cache�е�Ͱ���
			// �ͷ�span��page cacheʱ��ʹ��page cache�����Ϳ�����
			_spanList[index]._mtx.unlock();

			// ����Page Cacheǰ��Page Cache�Ĵ������м���
			PageCache::GetIntance()->_pageMtx.lock(); 
			PageCache::GetIntance()->ReleaseSpanToPageCache(span);
			PageCache::GetIntance()->_pageMtx.unlock();

			// ��span����Page Cache�󣬻�����������뵽������
			_spanList[index]._mtx.lock();
		}
		start = next;
	}

	_spanList[index]._mtx.unlock();
}