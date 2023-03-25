#pragma once

#include "Common.h"

//using std::cout;
//using std::endl;

//#ifdef _WIN32
//	#include <Windows.h>
//#else
//// Linux
//#endif


// ֱ��ȥ���ϰ�ҳ����ռ䣬����malloc
//inline static void* SystemAlloc(size_t kpage)
//{
//#ifdef _WIN32
//	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
//#else
//	// linux��brk mmap��
//#endif
//
//	if (ptr == nullptr)
//		throw std::bad_alloc();
//
//	return ptr;
//}

template<class T>
class ObjectPool
{
public:
	// �����ڴ�ռ�
	T* New()
	{
		T* obj = nullptr;

		// ����ʹ�û������ڴ������ٴ��ظ�����
		if (_freeList)
		{
			void* next = *((void**)_freeList);
			obj = (T*)_freeList;
			_freeList = next;
		}
		else
		{
			// һ��ʼ _remainBytesΪ0 ���� ʣ��� _remainBytes���������ٶ����ֽ���
			// ����Ҫ�����������ڴ�ռ�
			if (_remainBytes < sizeof(T))
			{
				// �������ֽ���
				_remainBytes = 128 * 1024;
				//_memory = (char*)malloc(_remainBytes);
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			// ���������ռ�
			obj = (T*)_memory;
			// ��Ҫע����Ƕ���Ŀռ�����Ҫ���Դ����һ��ָ��Ĵ�С
			// ��Ϊ�������Ŀռ���ͷ4/8���ֽ���Ҫ������һ������ĵ�ַ
			// ��32λ��ָ���С��4���ֽڣ�64λ����8���ֽڣ����Բ���void*�ķ�ʽ����
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}

		// ��λnew����ʾ����T�Ĺ��캯����ʼ��
		new(obj)T;

		return obj;
	}

	// �ͷ��ڴ�ռ�
	void Delete(T* obj)
	{
		// ��ʾ�������������������
		obj->~T();

		// ͷ�嵽��������ͷ4/8���ֽ�������һ����
		*(void**)obj = _freeList; // _freeListһ��ʼΪNULLҲ�ǿ��Ե�,���λ��ָ���
		// ������������ͷ�ڵ�
		_freeList = obj;
	}

private:
	char* _memory = nullptr;     // Ԥ���������ڴ��ָ�����ڴ��ָ��
	size_t _remainBytes = 0;     // ����ڴ����зֹ�������ʣ�µ��ڴ��ֽ���
	void* _freeList = nullptr;   // ָ������ͷź󷵻����Ŀռ����������ͷָ��
};

//// T����
//struct TreeNode
//{
//	int _val;
//	TreeNode* _left;
//	TreeNode* _right;
//	TreeNode(int val = 0)
//		: _val(val)
//		, _left(nullptr)
//		, _right(nullptr)
//	{}
//};
//
//void TestObjectPool()
//{
//	// �����ͷ���ѯ�Ĵ���
//	const size_t Rounds = 3;
//	// ÿһ����ѯ�Ĵ���
//	const size_t N = 1000000;
//
//	// 1. ������ѯ֮��new��delete��ʱ���ʱ
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//
//	size_t begin1 = clock();
//	for (int i = 0; i < Rounds; i++)
//	{
//		for (int j = 0; j < N; j++)
//		{
//			v1.push_back(new TreeNode);
//		}
//		for (int j = 0; j < N; j++)
//		{
//			delete v1[j];
//		}
//		v1.clear();
//	}
//	size_t end1 = clock();
//
//	// 2. ������ѯ֮���ڴ�صĺ�ʱ
//	std::vector<TreeNode*> v2;
//	v2.reserve(N);
//	ObjectPool<TreeNode> TNPool;
//	size_t begin2 = clock();
//	for (int i = 0; i < Rounds; i++)
//	{
//		for (int j = 0; j < N; j++)
//		{
//			v2.push_back(TNPool.New());
//		}
//		for (int j = 0; j < N; j++)
//		{
//			TNPool.Delete(v2[j]);
//		}
//		v2.clear();
//	}
//	size_t end2 = clock();
//
//	cout << "new and delete cost time:" << end1 - begin1 << endl;
//	cout << "object pool cost time:" << end2 - begin2 << endl;
//}