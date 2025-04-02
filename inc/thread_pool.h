// 线程池头文件
#ifndef THREAD_POOL_HEADER
#define THREAD_POOL_HEADER

// 线程池

#include <cstdint>
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "locker.h"

template <typename T>
class ThreadPool {
public:
    ThreadPool(int thread_number = 8, int max_requests = 10000);
    ~ThreadPool();
    // 向请求队列中添加任务
    bool append(T* request);

private:
    static void* worker(void* arg);
    void run();

    uint32_t m_thread_number;    // 线程池中的线程数
    uint32_t m_max_requests;     // 队列中允许的最大请求数
    pthread_t* m_threads;   // 描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue;  // 队列
    locker m_qlock;         // 对请求队列的互斥锁
    sem m_queuestat;        // 是否有任务需要处理
    bool m_running;         // 是否结束线程

};

template <typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_running(true), m_threads(NULL) {
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

template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads;
    m_running = false;
}

template <typename T>
bool ThreadPool<T>::append(T* request) {
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

template <typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    pool->run();
    return pool;
}

template <typename T>
void ThreadPool<T>::run() {
    while (m_running) {
        m_queuestat.wait();
        m_qlock.lock();
        // ------------- CRITICAL AREA --------
        if (m_workqueue.empty()) {
            m_qlock.unlock();
            continue;
        }
        T* request = m_workqueue.front();
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