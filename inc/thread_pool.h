// 线程池头文件
#ifndef THREAD_POOL_HEADER
#define THREAD_POOL_HEADER

// 线程池：
//  线程池使用生产者-消费者模型，主线程（生产者）通过互斥锁保护的任务队列提交任务，工作线程（消费者）通过信号量（m_queuestat）同步等待任务。
// 当任务入队时，主线程调用sem_post增加信号量，唤醒一个阻塞在sem_wait的工作线程。
//  子线程处理完HTTP解析（process()方法）后，会生成响应数据并存入写缓冲区，然后通过修改epoll事件为EPOLLOUT（例如调用modfd函数），触发主线程
// 的写事件监听。主线程监听到可写事件后，调用write()方法完成数据发送。
//  当任务队列满时（append()返回false），主线程应立即发送503 Service Unavailable响应并关闭连接。

#include <cstdint>
#include <cstdio>
#include <exception>
#include <list>
#include <pthread.h>

#include "locker.h"

template <typename T> class ThreadPool {
public:
  ThreadPool(int thread_number = 8, int max_requests = 1000);
  ~ThreadPool();
  // 向请求队列中添加任务
  bool append(T *request);

private:
  static void *worker(void *arg);
  void run();

  uint32_t m_thread_number; // 线程池中的线程数
  uint32_t m_max_requests;  // 队列中允许的最大请求数
  pthread_t *m_threads; // 描述线程池的数组，其大小为m_thread_number
  std::list<T *> m_workqueue; // 队列
  locker m_qlock;             // 对请求队列的互斥锁
  sem m_queuestat;            // 是否有任务需要处理
  bool m_running;             // 是否结束线程
};

template <typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests)
    : m_thread_number(thread_number), m_max_requests(max_requests),
      m_running(true), m_threads(NULL) {
  if ((thread_number <= 0) || (max_requests <= 0)) {
    throw std::exception();
  }
  m_threads = new pthread_t[m_thread_number];
  if (!m_threads) {
    throw std::exception();
  }

  // 创建thread_number个线程，并将它们都设置为脱离线程
  for (int i = 0; i < thread_number; i++) {
    printf("create thread %d\n", i);
    if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
      // 创建线程失败
      delete[] m_threads;
      throw std::exception();
    }
    if (pthread_detach(m_threads[i])) {
      delete[] m_threads;
      throw std::exception();
    }
  }
}

template <typename T> ThreadPool<T>::~ThreadPool() {
  delete[] m_threads;
  m_running = false;
}

template <typename T> bool ThreadPool<T>::append(T *request) {
  m_qlock.lock();
  // ------------- CRITICAL AREA --------
  if (m_workqueue.size() > m_max_requests) {
    m_qlock.unlock();
    return false;
  }
  m_workqueue.push_back(request);
  // ------------- EXITING --------------
  m_qlock.unlock();
  m_queuestat.post();
  return true;
}

template <typename T> void *ThreadPool<T>::worker(void *arg) {
  ThreadPool *pool = static_cast<ThreadPool *>(arg);
  pool->run();
  return pool;
}

template <typename T> void ThreadPool<T>::run() {
  while (m_running) {
    m_queuestat.wait();
    m_qlock.lock();
    // ------------- CRITICAL AREA --------
    if (m_workqueue.empty()) {
      m_qlock.unlock();
      continue;
    }
    T *request = m_workqueue.front();
    m_workqueue.pop_front();
    // ------------- EXITING --------------
    m_qlock.unlock();
    if (!request) {
      continue;
    }
    request->process();
  }
}

#endif