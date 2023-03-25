#define _CRT_SECURE_NO_WARNINGS 1

#include "PageCache.h"

PageCache PageCache::_sIntance;

Span* PageCache::NewSpan(size_t k)
{
	//assert(k > 0 && k < N_PAGE);
	assert(k > 0);

	// ����128ҳֱ���Ҷ����룬�����256kbС��128ҳ����PageCache����ͺ�
	if (k > N_PAGE - 1)
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		// ���ö����ڴ��New��������Span����ʱ��New����ͨ����λnewҲ�Ƕ�Span��������˳�ʼ����
		Span* span = _spanPool.New();
		// ͬ����span�ķ�ʽȥ�������ռ�
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		// ����ӳ���ϵ
		//_idSpanMap[span->_pageId] = span;

		// ���û������Ż�
		_idSpanMap.set(span->_pageId, span);
		return span;
	}

	// ��ȥ��Ӧ��Ͱ�п���û�зǿյ�span�����û����ȥ�Ҷ�����
	if (!_spanLists[k].Empty())
	{
		// �����Ե�kҳ��Ϊ��
		//return _spanLists->PopFront();
		//return _spanLists[k].PopFront();
		
		// ***ע��***�����k��PAgeCache��Ͱ��Ϊ�գ�����ʱ��ȻҪ����_pageId��span��ӳ��
		// ������ڵ���ʱ���Լ�����
		Span* kSpan = _spanLists[k].PopFront();
		for (size_t i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;
	}
	// �ߵ���˵����k��Ͱ�ǿյģ�����ȥ���һ�º�����Ͱ��û��
	for (size_t i = k + 1; i < N_PAGE; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();
			// ��nҳ��span�г�kҳ��span������Central Cache���ٽ�ʣ�µ�n-kҳ�ҵ�n-k���Ͱ��
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			// �г�kҳspan�󣬸���nspan
			nSpan->_pageId += k;
			nSpan->_n -= k;
			_spanLists[nSpan->_n].PushFront(nSpan);

			// �洢nSpan����βҳ����nSpan֮���ӳ�䣬����page cache�ϲ�spanʱ����ǰ��ҳ�Ĳ���
		/*	_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;*/

			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);
			// ����id��span��ӳ�䣬����central cache����С���ڴ�ʱ�����Ҷ�Ӧ��span
			for (size_t i = 0; i < kSpan->_n; i++)
			{
				// ���kҳ�Ĵ���ڴ����ʼҳ��_pageId��ʼ����������KSpan���span
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}
			return kSpan;
		}
	}
	// ���Page Cache���е�Ͱ�������ڷǿյ�span����ô����Ҫ�������ռ�128ҳ�Ŀռ�	
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(N_PAGE - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = N_PAGE - 1;
	// ������õĿռ���뵽Page Cache��
	_spanLists[bigSpan->_n].PushFront(bigSpan);
	// ��ʱPage Cache�е�N_PAGE - 1��Ͱһ�����ڷǿյ�span
	// �ݹ鸴�ô��뼴��
	return NewSpan(k);
}

// ��ȡ�Ӷ���span��ӳ��, ͨ����ַ����13λ�ķ�ʽ�õ���obj�����ҳ��
// ������ʼҳ�ſ�ʼ��kҳ��������һ��span��ͨ��ҳ�ź�_idSpanMap�����ҵ�obj�����Ӧ��span
Span* PageCache::MapObjectToSpan(void* obj)
{
	// �ȼ���ҳ�ţ�ǿתһ��
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
	//////////////////////////////////////////////////////
	//// �ȼ���ҳ�ţ�ǿתһ��
	//PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
	//// ���ж��ִ�������ʹ�ϣͰ����Ҫ������
	//std::unique_lock<std::mutex> lock(_pageMtx);   //����ʱ����������ʱ�Զ�����
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
// �ͷſ��е�span�ص�PageCache�����ϲ����ڵ�span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// ����128ҳֱ���ͷ�
	if (span->_n > N_PAGE - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		// ��Ҫ��new������spandelete��
		delete span;
		// ֱ�ӷ��أ���Ϊ��ֱ�ӻ����ѣ�����Ҫ�ϲ�
		return;
	}
	// ��span��ǰ��ҳ�����Խ��кϲ��������ڴ���Ƭ����
	// 1����ǰ�ϲ�
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;
		//// ����map���Ƿ����prevIdӳ��
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
		// �ߵ���˵��map�д���prevId��Ӧ��span, ������Ҫ�ж���ʹ��״̬
		//Span* prevSpan = ret->second;
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}
		// �ϲ�������128ҳ��span�޷����й���ֹͣ��ǰ�ϲ�
		if (prevSpan->_n + span->_n > N_PAGE - 1)
		{
			break;
		}
		// ������ǰ�ϲ�	
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		// ��ԭ��prevSpan�Ӷ�Ӧ��˫�������Ƴ�
		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		// ʹ�ö����ڴ�ص�deleteɾ��
		_spanPool.Delete(prevSpan);
	}
	// 2�����ϲ�
	while (true)
	{
		// ������һҳ����ʼҳ��
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
		// ��ʼ���ϲ�
		span->_n += nextSpan->_n;
		// ��ԭ��nextSpan�Ӷ�Ӧ��˫�������Ƴ�
		_spanLists[nextSpan->_n].Erase(nextSpan);

		//delete nextSpan;

		_spanPool.Delete(nextSpan);
	}
	// ���ϲ���span�ҵ�Page Cache�ж�Ӧ��˫������
	_spanLists[span->_n].PushFront(span);
	//// �ҽ���span��Page Cache����Ҫ����span������β��ӳ���ϵ
	//_idSpanMap[span->_pageId] = span;
	//// ע��˴�Ҫ��һ����1000ҳ������5ҳ����ô��βҳ����1004ҳ
	//_idSpanMap[span->_pageId + span->_n - 1] = span;

	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);

	// ����״̬����Ϊfalse
	span->_isUse = false;
}