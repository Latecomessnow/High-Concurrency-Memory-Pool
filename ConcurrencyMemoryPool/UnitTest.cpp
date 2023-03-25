#define _CRT_SECURE_NO_WARNINGS 1

#include "ObjectPool.h"
#include "ConcurrentAlloc.h"

void Alloc1()
{
	for (int i = 0; i < 5; i++)
	{
		void* ptr = ConcurrentAlloc(6);
	}
}

void Alloc2()
{
	for (int i = 0; i < 5; i++)
	{
		void* ptr = ConcurrentAlloc(7);
	}
}

void TLSTest()
{
	std::thread t1(Alloc1);
	std::thread t2(Alloc2);

	t1.join();
	t2.join();
}

void TestConcurrentAlloc()
{
	void* p1 = ConcurrentAlloc(1);
 	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(6);
	void* p4 = ConcurrentAlloc(7);
	void* p5 = ConcurrentAlloc(3);

	cout << p1 << endl;
	cout << p2 << endl;
	cout << p3 << endl;
	cout << p4 << endl;
	cout << p5 << endl;
}

void TestAddressShift()
{
	PAGE_ID id1 = 2000;
	PAGE_ID id2 = 2001;
	char* p1 = (char*)(id1 << PAGE_SHIFT);
	char* p2 = (char*)(id2 << PAGE_SHIFT);
	// 可以看出在这个区间内的所有对象都是属于2000页的
	// 但还不能确定这些还回来的自由链表属于哪一个span
	while (p1 < p2)
	{
		cout << (void*)p1 << " : " << ((PAGE_ID)p1 >> PAGE_SHIFT) << endl;
		p1 += 8;
	}
}

void TestCurrentDeallocate()
{
	void* p1 = ConcurrentAlloc(1);
	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(7);

	ConcurrentFree(p1);
	ConcurrentFree(p2);
	ConcurrentFree(p3);
}

void BigAlloc()
{
	// 大于256kb但小于128页找PageCache申请
	void* p1 = ConcurrentAlloc(257 * 1024);
	cout << p1 << endl;
	ConcurrentFree(p1);
	// 大于128页找堆申请
	void* p2 = ConcurrentAlloc(129 * 8 * 1024);
	cout << p2 << endl;
	ConcurrentFree(p2);
}

//int main()
//{
//	//TestObjectPool();
//	//TLSTest();
//	TestConcurrentAlloc();
//	//TestAddressShift();
//
//	//TestCurrentDeallocate();
//	BigAlloc();
//	return 0;
//}