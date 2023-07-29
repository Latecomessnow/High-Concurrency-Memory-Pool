# 项目简介
项目的整体框架是由三层缓存构成的，线程缓存，中心缓存和页缓存。整个项目的流程是这样的，申请内存的时候，先去线程缓存去要，线程缓存没有的话，再去中心缓存去要，中心缓存再没有的话，就去页缓存去要，而页缓存会预先向操作系统申请过量的内存资源; 释放内存的时候也是类似，先把内存还给线程缓存，中心缓存会在合适的时候去回收线程缓存中的内存，页缓存也会在合适的时候去回收中心缓存的内存。

## 1. 项目整体框架
  ![image](https://github.com/Latecomessnow/High-Concurrency-Memory-Pool/assets/101911487/2e682377-922c-44e8-b2dc-7fd0d42d898e)

## 2. 线程缓存结构
  ![image](https://github.com/Latecomessnow/High-Concurrency-Memory-Pool/assets/101911487/09b5eb08-706c-4d07-ba12-cb071b82557f)

## 3. 中心缓存结构
  ![image](https://github.com/Latecomessnow/High-Concurrency-Memory-Pool/assets/101911487/65e15d70-d4f5-4a7a-9abb-3dbb7590921c)

## 4. 页缓存结构
  ![image](https://github.com/Latecomessnow/High-Concurrency-Memory-Pool/assets/101911487/3b2dd611-1995-4efc-9690-e8257e750b0a)
