#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

// ����ģʽ
class PageCache
{
public:

	// Thread Cache��Central Cache���벻ͬ�ֽ����Ŀռ䣬��Central Cache�еĶ�ӦͰ��Ϊ��
	// ��ô��ʱ�������̶߳���ȥPage Cache�����룬���Ҵ�Page Cache���зֳ����Ĵ���ڴ棬
	// һ���ֻ�ҵ�Central Cache�е�span�У�ʣ�µĿռ仹�����ӵ�Page Cache�е�����λ�õ�Ͱ
	// ��������ͻ�ͬʱ����Page Cache��ͬλ��Ͱ�����ܹ�ʹ��Ͱ���������ڷ�����Щ��ͬλ�õ�Ͱʱ
	// �����ļ����ͽ������󽵵�Ч�ʣ����������Ƕ�����Page Cache���м���
	std::mutex _pageMtx;

	static PageCache* GetIntance()
	{
		return &_sIntance;
	}

	// ��ȡ�Ӷ���span��ӳ��, ͨ����ַ����13λ�ķ�ʽ�õ���obj�����ҳ��
	// ������ʼҳ�ſ�ʼ��kҳ��������һ��span��ͨ��ҳ�ź�_idSpanMap�����ҵ�obj�����Ӧ��span
	Span* MapObjectToSpan(void* obj);

	// Central Cache��û�зǿյ�span����Page Cache����
	// ��ȡһ��kҳ��span
	Span* NewSpan(size_t k);

	
	void ReleaseSpanToPageCache(Span* span);

private:
	PageCache()
	{}

	PageCache(const PageCache&) = delete;

private:
	// Page CacheҲ��һ��ÿ��λ�ô���SpanList��ϣͰ
	SpanList _spanLists[N_PAGE];

	static PageCache _sIntance;
	// ��Page Cache�����Central Cache��kҳ���ռ��Central Cache�е�span����ӳ���ϵ
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	// ʵ�û����������Ż�
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	ObjectPool<Span> _spanPool;
};