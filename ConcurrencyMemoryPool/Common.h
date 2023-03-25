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

// ����thread cache��������ֽ���Ϊ256kb
// С�ڵ���MAX_BYTES������thread cache����
// ����MAX_BYTES����ֱ����page cache����ϵͳ������
static const size_t MAX_BYTES = 256 * 1024;
// һ����280���������������Ͱ
static const size_t N_FREE_LIST = 208;
// Page Cache�ճ���һ��Ͱ���ã�129��Ͱ��ҳ��Ͱλ�þ�һһ��Ӧ����
static const size_t N_PAGE = 129;

// һҳ����Ϊ8k����ת����Ϊ13(2^13=8k)
static const size_t PAGE_SHIFT = 13;

// ֱ��ȥ���ϰ�ҳ����ռ䣬����malloc
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux��brk mmap��
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

//ֱ�ӽ��ڴ滹����
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//linux��sbrk unmmap��
#endif
}

// 32λ�½��̵�ַ�ռ�Ĵ�С��2^32, 64λ��2^64, ��8k(2^13)Ϊһҳ
// ��ô�ܹ����ڵ�ҳ����32:2^32/2^13=2^19ҳ, 64:2^64/2^13=2^51ҳ
#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;	
#else
	//Linunx...
#endif 

static inline void*& NextObj(void* obj)
{
	// ����obj�����ͷ4/8���ֽڣ��������������һ����ַ
	return *(void**)obj;
}

// 1. �����ϣͰ���зֺõ�С������������
class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);
		// ���������Ķ���ռ�ͷ�������������
		NextObj(obj) = _freeList;
		_freeList = obj;
		_size++;
	}

	void* Pop()
	{
		assert(_freeList);
		// �������������ÿռ�
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		_size--;
		return obj;
	}

	// ��һ��PushRange��pushʱ����push�ĸ������Ա��ڽ���_size�ļ���
	void PushRange(void* start, void* end, size_t n)
	{
		assert(start);
		assert(end); // end����Ϊ�ա���end��ͷ��4/8�ֽ�Ϊ��
		NextObj(end) = _freeList;
		// ���ǵ��Ժ��ֵ����⣬�˴��Ѵ�Central Cache�л�ȡ����һ����size�ֽڴ�С��
		// ��������ҵ�Thread Cache��Ӧ��Ͱ��,end��ͷ��4/8�ֽ�ָ��_freeList��
		// ��Ҫ����_freeList��ͷ������Ӧ����_freeList = �������ռ��start
		//start = _freeList;
		_freeList = start;
		_size += n;
	}

	// �ӵ�ǰͰ�е�����������ȡ��n�����󻹻ص����Ļ�����ȥ
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

	// ��Ϊ��ֵ���ص�_maxSize�ǿ�����������ģ����ܹ������޸ģ����Դ˴���Ҫ���÷���
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
	size_t _maxSize = 1;          // ��¼ÿһ����Central Cache �������������
	size_t _size = 0;             // ��¼���������еĸ���
};


// 2. ��������С�Ķ���ӳ�����
class SizeClass
{
public:
	// ������������10%���ҵ�����Ƭ�˷�
	// [1,128]					8byte����	     freelist[0,16)
	// [128+1,1024]				16byte����	     freelist[16,72)
	// [1024+1,8*1024]			128byte����	     freelist[72,128)
	// [8*1024+1,64*1024]		1024byte����      freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte����    freelist[184,208)

	// ��������ֽ���������Ӧ�Ķ������������Ҫ�����ֽ���
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
			// �������256kb�ֽ�����ֱ���Ҷ�Ҫ
			// ����255kb�İ�ҳ����
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}
	}


	// 2. ����д��
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		// ������7�ֽڣ�7+(8-1)=14, ~(8-1)=...111000������λ����0��
		// ���ǰѶ������Ķ�Ӧ�ı���λ��һ���������λȫ����0�����������ֽ���+������-1����
		// ���ܹ��������һ������������ȫ���������
		// �����ĸ�����λ����1����001110 & 111000 = 001000 == 8
		return (bytes + alignNum - 1) & ~(alignNum - 1); // ��������ֽ���
	}

	// 1. һ��д��
	// 7-----8
	// 14----8
	// 24----8
	//static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	//{
	//	size_t alignSize = 0;
	//	// Ҫ�����ֽ������Ƕ������ı���
	//	if (bytes % alignNum != 0)
	//	{
	//		alignSize = (bytes / alignNum + 1) * alignNum;
	//	}
	//	// Ҫ�����ֽ����Ƕ������ı���, ��16�����������8��ֱ�Ӹ�16�ֽھ͹���
	//	else
	//		alignSize = bytes;
	//	return alignSize;
	//}

	// ��ͨд��
	//static inline size_t _Index(size_t bytes, size_t alignNum)
	//{
	//	if (bytes % alignNum == 0)
	//	{
	//		return bytes / alignNum - 1;
	//	}
	//	else
	//	{
	//		// 9 / 8 = 1 �ڶ���Ͱ
	//		// ��18���ֽڣ�18 / 8 = 2�����Ƕ�Ӧ��������Ͱ
	//		return bytes / alignNum;
	//	}
	//}

	// ����д��
	// align_shiftָ���Ƕ�������ת�������������128��ת��������7(2^7==128)
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// һ����������䣬ǰ�ĸ������Ͱ��
		// ��Ϊ�������Ĳ�ͬ�����Ե���Ͱ��������ͬ����Ҫһ�������¼ÿ��������Ͱ������
		static int group_arrry[4] = { 16, 56, 56, 56 };
		if (bytes <= 128)
		{
			// �����Ƕ�������ת������2^3 == 8������
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024)
		{
			// ��Ҫ��ȥ��һ��������ֽ�����Ȼ����ͨ��ӳ��+��һ�����������ڵ�Ͱ��
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

	// thread cache һ�δ����Ļ����ȡ���ٸ�
	// ͨ������������������ǾͿ��Ը�����������Ķ���Ĵ�С�������������Ķ������
	// ���ҿ��Խ������Ķ���������Ƶ�2~512��֮�䡣Ҳ����˵������thread cacheҪ����Ķ�����С
	// �����һ���Ը���512�����󣻾���thread cacheҪ����Ķ����ٴ�������һ���Ը���2������
	static inline size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// ������һ���������256kb, ������Ҳ�������СΪ256kb����������ռ��Thread Cache
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}

	//central cacheһ����page cache��ȡ����ҳ
	static inline size_t NumMovePage(size_t size)
	{
		// �����thread cacheһ����central cache�������ĸ������� 
		size_t num = NumMoveSize(size);
		// num��size��С�Ķ�����������ֽ���
		size_t nPage = num * size;
		// �������ֽ���ת����ҳ��
		nPage >>= PAGE_SHIFT;
		// �������һҳ�������ٸ�һҳ
		if (nPage == 0)
			return 1;
		return nPage;
	}
};

// ����CentralCache�ж������ҳ����ڴ��Ƚṹ
struct Span
{
	PAGE_ID _pageId = 0;          // ����ڴ���ʼҳ��ҳ��
	size_t _n = 0;                 // ҳ������

	Span* _prev = nullptr;        // ˫������Ľṹ
	Span* _next = nullptr;
	
	size_t _useCount = 0;         // �ҽ��ڴ��Span�������������С���ڴ棬��¼�䱻�����thread cache������
	void* _freeList = nullptr;    // С���ڴ����������

	bool _isUse = false;          // �жϵ�ǰ��span�Ƿ�ʹ��
	// ��central cache��page cache���뵽һ��spanʱ����Ҫ��������span��_isUse��Ϊtrue��

	size_t _objSize = 0;          // span���кõ�С����Ĵ�С
};

// ÿ��span����Ķ���һ����ҳΪ��λ�Ĵ���ڴ棬ÿ��Ͱ���������span�ǰ���˫�������ʽ���������ģ�
// ����ÿ��span���滹��һ�������������������������ҵľ���һ�����к��˵��ڴ�飬
// ���������ڵĹ�ϣͰ��Щ�ڴ�鱻�г��˶�Ӧ�Ĵ�С��

// ��ͷ˫��ѭ������
class SpanList
{
public:
	SpanList()
	{
		// һ��ʼ��_head newһ��ռ������Ϊͷָ��
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

	// ��Page Cache����Ҫ����ϣͰ�е�Spanͷɾ��Central Cache
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	// ɾ��posλ��
	void Erase(Span* pos)
	{
		assert(pos);
		// ���ܹ�ɾ���ڱ�λ
		assert(pos != _head);
		// 1�������ϵ�
		// 2���鿴ջ֡
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
	// �ڱ�λָ��
	Span* _head;
public:
	// ����Central Cacheʱ��Ҫ��������
	// ��SpanList�ж�����������ȷ��Ceantral Cache �е�SpanList _spanLIsts[208]ÿ��Ͱ������

	std::mutex _mtx; // Ͱ��
};