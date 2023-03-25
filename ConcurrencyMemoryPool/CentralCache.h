#pragma once

#include "Common.h"

// 期望每一个thread cache找的都是同一个Central Cache申请空间
// 单例模式(饿汉模式)
class CentralCache
{
public:
	// 提供一个函数获取单例对象
	static CentralCache* GetInstance()
	{
		return &_sInstance;
	}

	// 获取一个非空的span
	Span* GetOneSpan(SpanList& list, size_t size);

	// 从中心缓存中获取一定数量的对象给Thread Cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t bytes);

	// 将一定数量的对象还给对应的span
	void ReleaseListToSpans(void* start, size_t size);

private:
	// 和thread cache 一样也是一个有着208的桶的哈希桶
	SpanList _spanList[N_FREE_LIST];
private:
	// 构造函数私有化实现单例
	CentralCache()
	{}

	// 防止拷贝构造
	CentralCache(const CentralCache&) = delete;

	// 饿汉模式一开始就创建好一个Central Cache对象
	static CentralCache _sInstance;
};

