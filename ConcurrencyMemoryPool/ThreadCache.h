#pragma once

#include "Common.h"

class ThreadCache
{
public:
	// ������ͷ��ڴ����
	void* Allocate(size_t size);

	// ��Ҫͨ��size�ҵ���Ӧλ�õ�Ͱ�����ͷ�
	void Deallocate(void* ptr, size_t size);

	void* FetchFromCentralCache(size_t index, size_t size);

	// �ͷŶ�����Thread Cache��������������������ڴ浽���Ļ���
	void ListTooLong(FreeList& list, size_t size);

private:
	// �����������, һ����208��Ͱ
	FreeList _freeLists[N_FREE_LIST];
};

// ʹ�ùؼ���_declspec(thread)����������ΪTLS����(Thread Local Storage)
// TLS ������һ���������͵ı�����ÿ���̶߳����Լ������Ĵ洢�ռ䣬
// ��ͬ�߳�֮�以�����������ͬʱ���ʲ�ͬ�ı�����������������,������̱߳���е��̰߳�ȫ���⡣
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;