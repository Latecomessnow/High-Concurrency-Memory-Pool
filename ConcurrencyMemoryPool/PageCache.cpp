#define _CRT_SECURE_NO_WARNINGS 1

#include "PageCache.h"

PageCache PageCache::_sIntance;

Span* PageCache::NewSpan(size_t k)
{
	//assert(k > 0 && k < N_PAGE);
	assert(k > 0);

	// 大于128页直接找堆申请，如果是256kb小于128页的找PageCache申请就好
	if (k > N_PAGE - 1)
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		// 利用定长内存池New函数申请Span对象时，New函数通过定位new也是对Span对象进行了初始化的
		Span* span = _spanPool.New();
		// 同样用span的方式去管理这块空间
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		// 建立映射关系
		//_idSpanMap[span->_pageId] = span;

		// 利用基数树优化
		_idSpanMap.set(span->_pageId, span);
		return span;
	}

	// 先去对应的桶中看有没有非空的span，如果没有再去找堆申请
	if (!_spanLists[k].Empty())
	{
		// 供调试第k页不为空
		//return _spanLists->PopFront();
		//return _spanLists[k].PopFront();
		
		// ***注意***如果第k个PAgeCache的桶不为空，返回时依然要建立_pageId和span的映射
		// 否则会在调试时把自己坑死
		Span* kSpan = _spanLists[k].PopFront();
		for (size_t i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;
	}
	// 走到这说明第k个桶是空的，可以去检查一下后续的桶有没有
	for (size_t i = k + 1; i < N_PAGE; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();
			// 从n页的span切出k页的span返还给Central Cache，再将剩下的n-k页挂到n-k这个桶上
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			// 切出k页span后，更新nspan
			nSpan->_pageId += k;
			nSpan->_n -= k;
			_spanLists[nSpan->_n].PushFront(nSpan);

			// 存储nSpan的首尾页号与nSpan之间的映射，方便page cache合并span时进行前后页的查找
		/*	_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;*/

			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);
			// 建立id和span的映射，方便central cache回收小块内存时，查找对应的span
			for (size_t i = 0; i < kSpan->_n; i++)
			{
				// 这个k页的大块内存从起始页号_pageId开始往后都是属于KSpan这个span
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}
			return kSpan;
		}
	}
	// 如果Page Cache所有的桶都不存在非空的span，那么就需要向堆申请空间128页的空间	
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(N_PAGE - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = N_PAGE - 1;
	// 将申请好的空间插入到Page Cache中
	_spanLists[bigSpan->_n].PushFront(bigSpan);
	// 此时Page Cache中的N_PAGE - 1号桶一定存在非空的span
	// 递归复用代码即可
	return NewSpan(k);
}

// 获取从对象到span的映射, 通过地址右移13位的方式得到该obj对象的页号
// 而从起始页号开始的k页都是属于一个span，通过页号和_idSpanMap就能找到obj对象对应的span
Span* PageCache::MapObjectToSpan(void* obj)
{
	// 先计算页号，强转一下
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
	//////////////////////////////////////////////////////
	//// 先计算页号，强转一下
	//PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
	//// 会有多个执行流访问哈希桶，需要加上锁
	//std::unique_lock<std::mutex> lock(_pageMtx);   //构造时加锁，析构时自动解锁
	////auto ret = _idSpanMap.find(id);
	//auto ret = _idSpanMap.get(id);
	//if (ret != _idSpanMap.end())
	//{
	//	return ret->second;
	//}
	//else
	//{
	//	assert(false);
	//	return nullptr;
	//}
}
// 释放空闲的span回到PageCache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 大于128页直接释放
	if (span->_n > N_PAGE - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		// 还要将new出来的spandelete掉
		delete span;
		// 直接返回，因为是直接还给堆，不需要合并
		return;
	}
	// 对span的前后页，尝试进行合并，缓解内存碎片问题
	// 1、向前合并
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;
		//// 查找map中是否存在prevId映射
		//auto ret = _idSpanMap.find(prevId);
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		 
		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			break;
		}
		// 走到这说明map中存在prevId对应的span, 但还需要判断其使用状态
		//Span* prevSpan = ret->second;
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}
		// 合并出超过128页的span无法进行管理，停止向前合并
		if (prevSpan->_n + span->_n > N_PAGE - 1)
		{
			break;
		}
		// 进行向前合并	
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		// 将原先prevSpan从对应的双链表中移除
		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		// 使用定长内存池的delete删除
		_spanPool.Delete(prevSpan);
	}
	// 2、向后合并
	while (true)
	{
		// 计算下一页的起始页号
		PAGE_ID nextId = span->_pageId + span->_n;
		//auto ret = _idSpanMap.find(nextId);
		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		//Span* nextSpan = ret->second;
		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}
		if (nextSpan->_n + span->_n > N_PAGE - 1)
		{
			break;
		}
		// 开始向后合并
		span->_n += nextSpan->_n;
		// 将原先nextSpan从对应的双链表中移除
		_spanLists[nextSpan->_n].Erase(nextSpan);

		//delete nextSpan;

		_spanPool.Delete(nextSpan);
	}
	// 将合并后span挂到Page Cache中对应的双链表中
	_spanLists[span->_n].PushFront(span);
	//// 挂接新span到Page Cache后需要建立span与其首尾的映射关系
	//_idSpanMap[span->_pageId] = span;
	//// 注意此处要减一，如1000页，管理5页，那么其尾页就是1004页
	//_idSpanMap[span->_pageId + span->_n - 1] = span;

	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);

	// 将其状态设置为false
	span->_isUse = false;
}