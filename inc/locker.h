#ifndef LOCKER_HEADER
#define LOCKER_HEADER
// 线程同步机制包装类
// 包含信号量、互斥体和条件变量

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量
class sem {
public:
    // 创建并初始化信号量
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    // 销毁信号量
    ~sem() {
        sem_destroy(&m_sem);
    }

    // 等待信号量
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    // 增加信号量
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

class cond {
public:
    // 条件变量初始化
    cond() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            // 释放互斥体资源
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    // 条件变量销毁
    ~cond() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
    // 等待条件变量
    bool wait() {
        int ret = 0;
        pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    // 唤醒等待条件变量的线程
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};


// 互斥体
class locker {
public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    // 加锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    // 解锁
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

#endif