#pragma once

#include <iostream>
#include <assert.h>
#include <thread>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <time.h>
#include <vector>

#ifdef _WIN32
	#include <Windows.h>
#else
	// Linux...
#endif

using std::cout;
using std::endl;

// 设置thread cache缓存最大字节数为256kb
// 小于等于MAX_BYTES，就找thread cache申请
// 大于MAX_BYTES，就直接找page cache或者系统堆申请
static const size_t MAX_BYTES = 256 * 1024;
// 一共有280个挂着自由链表的桶
static const size_t N_FREE_LIST = 208;
// Page Cache空出第一个桶不用，129个桶，页和桶位置就一一对应上了
static const size_t N_PAGE = 129;

// 一页设置为8k，其转换数为13(2^13=8k)
static const size_t PAGE_SHIFT = 13;

// 直接去堆上按页申请空间，跳过malloc
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap等
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

//直接将内存还给堆
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//linux下sbrk unmmap等
#endif
}

// 32位下进程地址空间的大小是2^32, 64位是2^64, 以8k(2^13)为一页
// 那么总共存在的页号有32:2^32/2^13=2^19页, 64:2^64/2^13=2^51页
#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;	
#else
	//Linunx...
#endif 

static inline void*& NextObj(void* obj)
{
	// 返回obj对象的头4/8个字节，即自由链表的下一个地址
	return *(void**)obj;
}

// 1. 管理哈希桶中切分好的小对象自由链表
class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);
		// 将还回来的对象空间头插进自由链表中
		NextObj(obj) = _freeList;
		_freeList = obj;
		_size++;
	}

	void* Pop()
	{
		assert(_freeList);
		// 从自由链表中拿空间
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		_size--;
		return obj;
	}

	// 改一下PushRange，push时传入push的个数，以便于进行_size的计算
	void PushRange(void* start, void* end, size_t n)
	{
		assert(start);
		assert(end); // end并不为空。是end的头上4/8字节为空
		NextObj(end) = _freeList;
		// 又是调试后发现的问题，此处把从Central Cache中获取到的一个个size字节大小的
		// 自由链表挂到Thread Cache对应的桶中,end的头上4/8字节指向_freeList后
		// 需要更新_freeList的头，所以应该是_freeList = 还回来空间的start
		//start = _freeList;
		_freeList = start;
		_size += n;
	}

	// 从当前桶中的自由链表中取出n个对象还回到中心缓存中去
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n <= _size);
		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}

	// 因为传值返回的_maxSize是拷贝构造回来的，不能够进行修改，所以此处需要引用返回
	size_t& GetMaxSize()
	{
		return _maxSize;
	}

	
	size_t Size()
	{
		return _size;
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;          // 记录每一次向Central Cache 批量申请的数量
	size_t _size = 0;             // 记录自由链表中的个数
};


// 2. 计算对象大小的对齐映射规则
class SizeClass
{
public:
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]					8byte对齐	     freelist[0,16)
	// [128+1,1024]				16byte对齐	     freelist[16,72)
	// [1024+1,8*1024]			128byte对齐	     freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐      freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐    freelist[184,208)

	// 计算给定字节数经过相应的对齐数对齐后需要的总字节数
	static inline size_t RoundUp(size_t bytes)
	{
		if (bytes <= 128)
			return _RoundUp(bytes, 8);

		else if (bytes <= 1024)
			return _RoundUp(bytes, 16);

		else if (bytes <= 8 * 1024)
			return _RoundUp(bytes, 128);

		else if (bytes <= 64 * 1024)
			return _RoundUp(bytes, 1024);

		else if (bytes <= 256 * 1024)
			return _RoundUp(bytes, 8 * 1024);
		else
		{
			// 申请大于256kb字节数的直接找堆要
			// 大于255kb的按页对齐
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}
	}


	// 2. 大佬写法
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		// 如申请7字节，7+(8-1)=14, ~(8-1)=...111000，后三位都是0，
		// 就是把对齐数的对应的比特位置一，后边所有位全部清0，在与申请字节数+对齐数-1相与
		// 就能够做到多给一个对齐数，补全对齐的作用
		// 但第四个比特位都是1，即001110 & 111000 = 001000 == 8
		return (bytes + alignNum - 1) & ~(alignNum - 1); // 对齐后总字节数
	}

	// 1. 一般写法
	// 7-----8
	// 14----8
	// 24----8
	//static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	//{
	//	size_t alignSize = 0;
	//	// 要申请字节数不是对齐数的倍数
	//	if (bytes % alignNum != 0)
	//	{
	//		alignSize = (bytes / alignNum + 1) * alignNum;
	//	}
	//	// 要申请字节数是对齐数的倍数, 如16，其对齐数是8，直接给16字节就够了
	//	else
	//		alignSize = bytes;
	//	return alignSize;
	//}

	// 普通写法
	//static inline size_t _Index(size_t bytes, size_t alignNum)
	//{
	//	if (bytes % alignNum == 0)
	//	{
	//		return bytes / alignNum - 1;
	//	}
	//	else
	//	{
	//		// 9 / 8 = 1 第二个桶
	//		// 如18个字节，18 / 8 = 2，就是对应到第三个桶
	//		return bytes / alignNum;
	//	}
	//}

	// 大佬写法
	// align_shift指的是对齐数的转换数，如对齐数128其转换数就是7(2^7==128)
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// 一共有五个区间，前四个区间的桶数
		// 因为对齐数的不同，所以导致桶的数量不同，需要一个数组记录每个区间中桶的数量
		static int group_arrry[4] = { 16, 56, 56, 56 };
		if (bytes <= 128)
		{
			// 传的是对齐数的转换，如2^3 == 8对齐数
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024)
		{
			// 需要减去上一个区间的字节数，然后再通过映射+上一个区间所存在的桶数
			return _Index(bytes - 128, 4) + group_arrry[0];
		}
		else if (bytes <= 8 * 1024)
		{
			return _Index(bytes - 1024, 7) + group_arrry[1] + group_arrry[0];
		}
		else if (bytes <= 64 * 1024)
		{
			return _Index(bytes - 8 * 1024, 10) + group_arrry[2] + group_arrry[1] + group_arrry[0];
		}
		else if (bytes <= 256 * 1024)
		{
			return _Index(bytes - 64 * 1024, 13) + group_arrry[3] + group_arrry[2] + group_arrry[1] + group_arrry[0];
		}
		else
		{
			assert(false);
			return -1;
		}
	}

	// thread cache 一次从中心缓存获取多少个
	// 通过下面这个函数，我们就可以根据所需申请的对象的大小计算出具体给出的对象个数
	// 并且可以将给出的对象个数控制到2~512个之间。也就是说，就算thread cache要申请的对象再小
	// 我最多一次性给出512个对象；就算thread cache要申请的对象再大，我至少一次性给出2个对象
	static inline size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// 就算我一次申请的是256kb, 我至少也会给出大小为256kb的两个对象空间给Thread Cache
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}

	//central cache一次向page cache获取多少页
	static inline size_t NumMovePage(size_t size)
	{
		// 计算出thread cache一次向central cache申请对象的个数上限 
		size_t num = NumMoveSize(size);
		// num个size大小的对象所需的总字节数
		size_t nPage = num * size;
		// 将所需字节数转换成页数
		nPage >>= PAGE_SHIFT;
		// 如果不够一页，则至少给一页
		if (nPage == 0)
			return 1;
		return nPage;
	}
};

// 管理CentralCache中多个连续页大块内存跨度结构
struct Span
{
	PAGE_ID _pageId = 0;          // 大块内存起始页的页号
	size_t _n = 0;                 // 页的数量

	Span* _prev = nullptr;        // 双向链表的结构
	Span* _next = nullptr;
	
	size_t _useCount = 0;         // 挂接在大块Span后的自由链表中小块内存，记录其被分配给thread cache的数量
	void* _freeList = nullptr;    // 小块内存的自由链表

	bool _isUse = false;          // 判断当前的span是否被使用
	// 当central cache向page cache申请到一个span时，需要立即将该span的_isUse改为true。

	size_t _objSize = 0;          // span中切好的小对象的大小
};

// 每个span管理的都是一个以页为单位的大块内存，每个桶里面的若干span是按照双链表的形式链接起来的，
// 并且每个span里面还有一个自由链表，这个自由链表里面挂的就是一个个切好了的内存块，
// 根据其所在的哈希桶这些内存块被切成了对应的大小。

// 带头双向循环链表
class SpanList
{
public:
	SpanList()
	{
		// 一开始给_head new一块空间出来作为头指针
		_head = new Span;
		//_head = _spanPool.New();
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}
	
	void PushFront(Span* newSpan)
	{
		Insert(Begin(), newSpan);
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	// 在Page Cache中需要将哈希桶中的Span头删给Central Cache
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	// 删除pos位置
	void Erase(Span* pos)
	{
		assert(pos);
		// 不能够删除哨兵位
		assert(pos != _head);
		// 1、条件断点
		// 2、查看栈帧
		//if (pos == _head)
		//{
		//	int i = 0;
		//}

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

private:
	// 哨兵位指针
	Span* _head;
public:
	// 访问Central Cache时需要加锁保护
	// 在SpanList中定义锁，可以确保Ceantral Cache 中的SpanList _spanLIsts[208]每个桶都有锁

	std::mutex _mtx; // 桶锁
};