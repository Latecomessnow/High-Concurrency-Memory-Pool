#define _CRT_SECURE_NO_WARNINGS 1

#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::Allocate(size_t bytes)
{
	assert(bytes <= MAX_BYTES);
	size_t alignBytes = SizeClass::RoundUp(bytes);
	size_t index = SizeClass::Index(bytes);
	// ����ڸ�����λ�ò�Ϊ�գ����������ÿռ�
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	// �����λ����������Ϊ�գ����CentralCache�л�ȡ�ռ�
	else
	{
		return FetchFromCentralCache(index, alignBytes);
	}
}

// �ͷŶ�����Thread Cache��������������������ڴ浽���Ļ���
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	// start, end��PopRange������Ͳ���
	void* start = nullptr;
	void* end = nullptr;
	// ��list��ȡ��һ�����������Ķ���
	list.PopRange(start, end, list.GetMaxSize());

	// ��ȡ���Ķ��󻹻ص�Central Cache��Ӧ��span��
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}

// ���뱻�ͷŶ�����ֽ�����Ϊ�˸����ֽ����ҵ���Ӧ��Ͱ����Push�黹�ռ�
void ThreadCache::Deallocate(void* ptr, size_t bytes)
{
	assert(ptr);
	assert(bytes <= MAX_BYTES);
	// �ҵ������ֽ���ӳ���ThreadCache��Ͱ������Push����������ռ�黹����������
	size_t index = SizeClass::Index(bytes);
	_freeLists[index].Push(ptr);

	// �����������ȴ���һ����������Ķ������ʱ�Ϳ�ʼ��һ��list��central cache
	if (_freeLists[index].Size() >= _freeLists[index].GetMaxSize())
	{
		ListTooLong(_freeLists[index], bytes);
	}
}

// ����index��Central Cache��Ҳ����Ӧ��Ͱλ��ȥ�ÿռ�
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// ����ʼ���������㷨
	// 1���ʼ����һ����central cacheһ������Ҫ̫�࣬��ΪҪ̫���˿����ò���
	// 2������㲻Ҫ���size��С�ڴ�������ôbatchNum�ͻ᲻��������ֱ������
	// 3��sizeԽ��һ����central cacheҪ��batchNum��ԽС
	// 4��sizeԽС��һ����central cacheҪ��batchNum��Խ��

	// Central Cache��thread cache������ֽڿռ���
	// windows�µ�min�����㷨�����min��ͻ
	size_t batchNum = min(_freeLists[index].GetMaxSize(), SizeClass::NumMoveSize(size));
	if (batchNum == _freeLists[index].GetMaxSize())
	{
		_freeLists[index].GetMaxSize() += 1;
	}

	// strat end������Ͳ�������ȡ��Central Cache���õ����зֺõ����������ͷβָ��
	void* start = nullptr;
	void* end = nullptr;

	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	// ����Ҫ��һ��, ����Ҫ����0
	assert(actualNum > 0);

	if (actualNum == 1)
	{
		// ���뵽����ĸ�����һ������ֱ�ӽ���һ�����󷵻ظ�����ռ���̼߳���
		assert(start == end);
		return start;
	}
	else
	{
		// ���뵽����ĸ����Ƕ��������Ҫ��ʣ�µĶ���ҵ�thread cache�ж�Ӧ�Ĺ�ϣͰ��
		// ��һ��start�ǿ�ռ�ͷ��ظ�����ռ���߳�
		// �˴�push����������Ķ���ĸ�����Ҫ��һ����Ϊ��һ�����󷵸�������ռ���߳�
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}