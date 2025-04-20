#ifndef LOCKFREE_HEADER
#define LOCKFREE_HEADER

#include <vector>
#include <atomic>
#include <stdexcept>

// 适用于单生产者单消费者的无锁队列
// 原理：首先队列采用RingBuffer（环形数组），这使得我们只需要修改writer_ptr和reader_ptr即可实现生产者的写入和消费者的取出。
//  之后，writer_ptr表示生产者写入位置，只能由生产者修改；生产者先把资源写入队列，再修改writer_ptr，则消费者始终看到一致的数据结构。
//  类似，reader_ptr只能由消费者修改；消费者先读取资源，再修改reader_ptr
//  当队列满时无法存入，队列空时无法取出，我们预留一个位置以确保队列的状态正确（即当资源数=N-1时队列满）
//  具体实现时，规定writer_ptr为生产者下一个写入的位置；reader_ptr为消费者下一个读取的位置；
//  writer_ptr == reader_ptr时队列空，writer_ptr + 1 == reader_ptr时队列满
template <typename T>
class LockFreeQueue_SPSC {
    std::vector<T> q;
    std::atomic_int writer_ptr;
    std::atomic_int reader_ptr;
public:
    LockFreeQueue_SPSC() {
        // dummy ctor
        // 由于该类不可拷贝+不可移动（因为atomic类型），我们先放一个假构造函数在这……
        // 之后再调用新的init_queue()函数来完成真正的队列初始化
    }
    void init_queue(int len) {
        q.resize(len);
        writer_ptr.store(0);
        reader_ptr.store(0);
    }
    LockFreeQueue_SPSC(int queue_len) : q(queue_len), writer_ptr(0), reader_ptr(0) {
        // ctor
        if (queue_len < 2) throw std::invalid_argument("queue len must be greater than 1");
    }
    LockFreeQueue_SPSC(const LockFreeQueue_SPSC&) = delete;
    const LockFreeQueue_SPSC operator=(const LockFreeQueue_SPSC&) = delete;

    bool empty() const {
        return writer_ptr.load(std::memory_order_acquire) == reader_ptr.load(std::memory_order_acquire);
    }
    bool full() const {
        return (writer_ptr.load(std::memory_order_acquire) + 1) % q.size() == reader_ptr.load(std::memory_order_acquire);
    }
    bool push(const T& item) {
        int writer = writer_ptr.load(std::memory_order_relaxed);
        int next_writer = (writer + 1) % q.size();
        if (next_writer == reader_ptr.load(std::memory_order_acquire)) {
            return false;   // 满
        }
        q[writer] = item;
        writer_ptr.store(next_writer, std::memory_order_release);
        return true;
    }
    bool pop(T& item_out) {
        int reader = reader_ptr.load(std::memory_order_relaxed);
        if (reader == writer_ptr.load(std::memory_order_acquire)) {
            return false;
        }
        item_out = q[reader];
        int next_reader = (reader + 1) % q.size();
        reader_ptr.store(next_reader, std::memory_order_release);
        return true;
    }
    std::size_t size() const {
        return (writer_ptr.load(std::memory_order_relaxed) - reader_ptr.load(std::memory_order_relaxed) + q.size()) % q.size();
    }
    std::size_t capacity() const {
        return q.size() - 1;
    }
};

#endif