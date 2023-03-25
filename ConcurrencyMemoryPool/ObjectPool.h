#pragma once

#include "Common.h"

//using std::cout;
//using std::endl;

//#ifdef _WIN32
//	#include <Windows.h>
//#else
//// Linux
//#endif


// 直接去堆上按页申请空间，跳过malloc
//inline static void* SystemAlloc(size_t kpage)
//{
//#ifdef _WIN32
//	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
//#else
//	// linux下brk mmap等
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
	// 申请内存空间
	T* New()
	{
		T* obj = nullptr;

		// 优先使用还回来内存块对象，再次重复利用
		if (_freeList)
		{
			void* next = *((void**)_freeList);
			obj = (T*)_freeList;
			_freeList = next;
		}
		else
		{
			// 一开始 _remainBytes为0 或者 剩余的 _remainBytes不够待开辟对象字节数
			// 都需要重新申请大块内存空间
			if (_remainBytes < sizeof(T))
			{
				// 待申请字节数
				_remainBytes = 128 * 1024;
				//_memory = (char*)malloc(_remainBytes);
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			// 分配给对象空间
			obj = (T*)_memory;
			// 需要注意的是对象的空间至少要可以存的下一个指针的大小
			// 因为还回来的空间中头4/8的字节需要链接下一个链表的地址
			// 在32位下指针大小是4个字节，64位下是8个字节，所以采用void*的方式即可
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}

		// 定位new，显示调用T的构造函数初始化
		new(obj)T;

		return obj;
	}

	// 释放内存空间
	void Delete(T* obj)
	{
		// 显示调用析构函数清理对象
		obj->~T();

		// 头插到自由链表，头4/8个字节链接下一链表
		*(void**)obj = _freeList; // _freeList一开始为NULL也是可以的,最后位置指向空
		// 更新自由链表头节点
		_freeList = obj;
	}

private:
	char* _memory = nullptr;     // 预先申请大块内存后，指向大块内存的指针
	size_t _remainBytes = 0;     // 大块内存在切分过程中所剩下的内存字节数
	void* _freeList = nullptr;   // 指向对象释放后返回来的空间的自由链表头指针
};

//// T类型
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
//	// 申请释放轮询的次数
//	const size_t Rounds = 3;
//	// 每一轮轮询的次数
//	const size_t N = 1000000;
//
//	// 1. 测试轮询之后new、delete的时间耗时
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
//	// 2. 测试轮询之后内存池的耗时
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