#pragma once

#include "Common.h"

// ����ÿһ��thread cache�ҵĶ���ͬһ��Central Cache����ռ�
// ����ģʽ(����ģʽ)
class CentralCache
{
public:
	// �ṩһ��������ȡ��������
	static CentralCache* GetInstance()
	{
		return &_sInstance;
	}

	// ��ȡһ���ǿյ�span
	Span* GetOneSpan(SpanList& list, size_t size);

	// �����Ļ����л�ȡһ�������Ķ����Thread Cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t bytes);

	// ��һ�������Ķ��󻹸���Ӧ��span
	void ReleaseListToSpans(void* start, size_t size);

private:
	// ��thread cache һ��Ҳ��һ������208��Ͱ�Ĺ�ϣͰ
	SpanList _spanList[N_FREE_LIST];
private:
	// ���캯��˽�л�ʵ�ֵ���
	CentralCache()
	{}

	// ��ֹ��������
	CentralCache(const CentralCache&) = delete;

	// ����ģʽһ��ʼ�ʹ�����һ��Central Cache����
	static CentralCache _sInstance;
};

