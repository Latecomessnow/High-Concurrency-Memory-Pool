#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

// 单例模式
class PageCache
{
public:

	// Thread Cache向Central Cache申请不同字节数的空间，而Central Cache中的对应桶都为空
	// 那么此时这两个线程都会去Page Cache中申请，而且从Page Cache中切分出来的大块内存，
	// 一部分会挂到Central Cache中的span中，剩下的空间还会链接到Page Cache中的其他位置的桶
	// 所以这里就会同时访问Page Cache不同位置桶，不能够使用桶锁，否则在访问这些不同位置的桶时
	// 大量的加锁和解锁会大大降低效率，所以这里是对整个Page Cache进行加锁
	std::mutex _pageMtx;

	static PageCache* GetIntance()
	{
		return &_sIntance;
	}

	// 获取从对象到span的映射, 通过地址右移13位的方式得到该obj对象的页号
	// 而从起始页号开始的k页都是属于一个span，通过页号和_idSpanMap就能找到obj对象对应的span
	Span* MapObjectToSpan(void* obj);

	// Central Cache中没有非空的span，向Page Cache申请
	// 获取一个k页的span
	Span* NewSpan(size_t k);

	
	void ReleaseSpanToPageCache(Span* span);

private:
	PageCache()
	{}

	PageCache(const PageCache&) = delete;

private:
	// Page Cache也是一个每个位置存着SpanList哈希桶
	SpanList _spanLists[N_PAGE];

	static PageCache _sIntance;
	// 在Page Cache分配给Central Cache的k页大块空间和Central Cache中的span建立映射关系
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	// 实用基数树进行优化
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	ObjectPool<Span> _spanPool;
};