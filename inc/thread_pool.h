// 线程池头文件
#ifndef THREAD_POOL_HEADER
#define THREAD_POOL_HEADER

// 线程池：
//  线程池使用生产者-消费者模型，主线程（生产者）通过互斥锁保护的任务队列提交任务，工作线程（消费者）通过信号量（m_queuestat）同步等待任务。
// 当任务入队时，主线程调用sem_post增加信号量，唤醒一个阻塞在sem_wait的工作线程。
//  子线程处理完HTTP解析（process()方法）后，会生成响应数据并存入写缓冲区，然后通过修改epoll事件为EPOLLOUT（例如调用modfd函数），触发主线程
// 的写事件监听。主线程监听到可写事件后，调用write()方法完成数据发送。
//  当任务队列满时（append()返回false），主线程应表示暂时无法完成请求任务。

#include <cstdint>
#include <cstdio>
#include <exception>
#include <list>
#include <pthread.h>

#include "locker.h"


// #define USE_LOCKFREE_QUEUE
#define USE_BOOST_LOCKFREE_QUEUE

#ifdef USE_LOCKFREE_QUEUE
#include "lockfree.h"
#endif


template <typename T> class ThreadPool {
  struct ThreadArg {
    int thread_id;
    ThreadPool* instance;
  };
public:
  ThreadPool(int thread_number = 1, int max_requests = 1000);
  ~ThreadPool();
  // 向请求队列中添加任务
  bool append(T *request);

private:
  static void *worker(void *arg);
  void run(int thread_id);

  uint32_t m_thread_number; // 线程池中的线程数
  uint32_t m_max_requests;  // 队列中允许的最大请求数
  pthread_t *m_threads; // 描述线程池的数组，其大小为m_thread_number
  std::list<T *> m_workqueue; // 队列
  locker m_qlock;             // 对请求队列的互斥锁
  sem m_queuestat;            // 是否有任务需要处理
  bool m_running;             // 是否结束线程
#ifdef USE_LOCKFREE_QUEUE
  int q_counter = 0;  // 用于轮询的计数器
  std::vector<sem> m_lf_queuestat;
  std::vector<
  LockFreeQueue_SPSC<T*>> m_lockfree_workq_set; // 无锁队列
#endif  // USE_LOCKFREE_QUEUE
};

template <typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests)
    : m_thread_number(thread_number), m_max_requests(max_requests),
      m_running(true), m_threads(NULL) 
#ifdef USE_LOCKFREE_QUEUE
       , m_lockfree_workq_set(thread_number) // empty
       , m_lf_queuestat(thread_number)
#endif  // USE_LOCKFREE_QUEUE
      {
  if ((thread_number <= 0) || (max_requests <= 0)) {
    throw std::exception();
  }
  m_threads = new pthread_t[m_thread_number];
  if (!m_threads) {
    throw std::exception();
  }

#ifdef USE_LOCKFREE_QUEUE
  // 无锁队列组合的构造
  for (int i = 0; i < thread_number; i++) {
    m_lockfree_workq_set[i].init_queue(max_requests);
  }
#endif

  // 创建thread_number个线程，并将它们都设置为脱离线程
  for (int i = 0; i < thread_number; i++) {
    printf("create thread %d\n", i);
    if (pthread_create(m_threads + i, NULL, worker, new ThreadArg{i, this}) != 0) {
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
#ifdef USE_LOCKFREE_QUEUE

template <typename T> bool ThreadPool<T>::append(T *request) {
  q_counter = (q_counter + 1) % m_thread_number;
  bool ret = m_lockfree_workq_set[q_counter].push(request);
  if (ret) {
    m_lf_queuestat[q_counter].post();
  }
  return ret;
}

template <typename T> void ThreadPool<T>::run(int thread_id) {
  T *request = nullptr;
  while (m_running) {
    m_lf_queuestat[thread_id].wait();
    if (m_lockfree_workq_set[thread_id].pop(request)) {
      request->process();
    }
  }
}
#else // USE_LOCKFREE_QUEUE
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

template <typename T> void ThreadPool<T>::run([[maybe_unused]]int thread_id) {
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
#endif  // USE_LOCKFREE_QUEUE

template <typename T> void *ThreadPool<T>::worker(void *arg) {
  ThreadArg* thread_arg = static_cast<ThreadArg*>(arg);
  ThreadPool *pool = static_cast<ThreadPool *>(thread_arg->instance);
  pool->run(thread_arg->thread_id);
  delete thread_arg;
  return pool;
}

#endif