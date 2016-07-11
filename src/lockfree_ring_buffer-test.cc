#include "yuki/utils.h"
#include "gtest/gtest.h"
#include "glog/logging.h"
#include <atomic>
#include <thread>

namespace yukino {

template<class T>
class LockFreeRingBuffer {
public:
    static const int kDefaultSpinCount = 2 * 1024;

    LockFreeRingBuffer(size_t capacity)
        : capacity_(capacity)
        , start_(0)
        , end_(0) {
        DCHECK(capacity_ > 0 && yuki::IsPowerOf2(capacity_));
        elems_ = new T[capacity_];
    }

    ~LockFreeRingBuffer() { delete[] elems_; }

    size_t Advance(size_t p) const {
        /* start and end pointers incrementation is done modulo 2*size */
        return (p + 1) & (2 * capacity_ - 1);
    }

    /* This inverts the most significant bit of start before comparison */
    bool IsFull() const {
        return end_.load() == (start_.load() ^ capacity_);
    }

    bool IsEmpty() const {
        return start_.load() == end_.load();
    }

    void Overwrite(const T &elem) {
        elems_[end_.load() & (capacity_ - 1)] = elem;
        if (IsFull()) { /* full, overwrite moves start pointer */
            start_.store(Advance(start_.load()));
        } else {
            end_.store(Advance(end_.load()));
        }
    }

    ////
    // end
    ////
    bool Feed(const T &elem) {
        if (IsFull()) {
            return false;
        }

        auto expected = end_.load(std::memory_order_relaxed);
        elems_[expected & (capacity_ - 1)] = elem;
        if (!IsFull()) {
            size_t new_pos;
            do {
                new_pos = Advance(end_.load());
            } while (!std::atomic_compare_exchange_strong(&end_, &expected,
                                                          new_pos));
        }
        return true;
    }

    ////
    // start
    ////
    bool Take(T *elem) {
        if (IsEmpty()) {
            return false;
        }

        auto expected = start_.load(std::memory_order_relaxed);
        *elem = elems_[expected & (capacity_ - 1)];
        size_t new_pos;
        do {
            new_pos = Advance(start_.load());
        } while (!std::atomic_compare_exchange_strong(&start_, &expected,
                                                     new_pos));
        return true;
    }

    void Push(const T &elem) {
        // "start_" can not be modify in writer threads.
        size_t expected, start;
        for (;;) {
            start = start_.load(std::memory_order_relaxed);
            expected = end_.load(std::memory_order_relaxed);
            if (expected != (start ^ capacity_)) { // is not full?
                elems_[expected & (capacity_ - 1)] = elem;
                break;
            }

            for (int n = 1; n < kDefaultSpinCount; n <<= 1) {
                for (int i = 0; i < n; i++) {
                    __asm__ ("pause");
                }

                start = start_.load(std::memory_order_relaxed);
                expected = end_.load(std::memory_order_relaxed);
                if (expected != (start ^ capacity_)) { // is not full?
                    elems_[expected & (capacity_ - 1)] = elem;
                    break;
                }
            }

            std::this_thread::yield();
        }
        if (expected != (start ^ capacity_)) { // is not full?
            size_t new_pos;
            do {
                new_pos = Advance(end_.load());
            } while (!std::atomic_compare_exchange_strong(&end_, &expected,
                                                          new_pos));
        }
    }

    void Get(T *elem) {
        // "end_" can not be modify in reader threads.
        size_t expected, end;
        for (;;) {
            end = end_.load(std::memory_order_relaxed);
            expected = start_.load(std::memory_order_relaxed);
            if (expected != end) { // is not empty?
                *elem = elems_[expected & (capacity_ - 1)];
                break;
            }

            for (int n = 1; n < kDefaultSpinCount; n <<= 1) {
                for (int i = 0; i < n; i++) {
                    __asm__ ("pause"); // spin await
                }

                end = end_.load(std::memory_order_relaxed);
                expected = start_.load(std::memory_order_relaxed);
                if (expected != end) { // is not empty?
                    *elem = elems_[expected & (capacity_ - 1)];
                    break;
                }
            }
            std::this_thread::yield();
        }

        size_t new_pos;
        do {
            new_pos = Advance(start_.load());
        } while (!std::atomic_compare_exchange_strong(&start_, &expected,
                                                      new_pos));
    }

private:
    const size_t capacity_;
    std::atomic<size_t> start_;
    std::atomic<size_t> end_;

    T *elems_;
};

TEST(LockFreeRingBufferTest, Sanity) {
    LockFreeRingBuffer<int> buf(8);

    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());
    buf.Overwrite(111);
    EXPECT_FALSE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());

    int value;
    EXPECT_TRUE(buf.Take(&value));
    EXPECT_EQ(111, value);
}

TEST(LockFreeRingBufferTest, FeedTake) {
    const int N = 8;
    LockFreeRingBuffer<int> buf(N);

    for (int i = 0; i < N; i++) {
        ASSERT_TRUE(buf.Feed(i));
    }
    EXPECT_TRUE(buf.IsFull());
    EXPECT_FALSE(buf.IsEmpty());
    ASSERT_FALSE(buf.Feed(9));

    for (int i = 0; i < N; i++) {
        int value;
        ASSERT_TRUE(buf.Take(&value));
        EXPECT_EQ(i, value);
    }
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());
}

TEST(LockFreeRingBufferTest, PushGet) {
    const int N = 8;
    LockFreeRingBuffer<int> buf(N);

    for (int i = 0; i < N; i++) {
        buf.Push(i);
    }
    EXPECT_TRUE(buf.IsFull());
    EXPECT_FALSE(buf.IsEmpty());

    for (int i = 0; i < N; i++) {
        int value;
        buf.Get(&value);
        EXPECT_EQ(i, value);
    }
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());
}

TEST(LockFreeRingBufferTest, OneByOneProducerConsumer) {
    LockFreeRingBuffer<int> buf(8);

    const int N = 10000;

    std::thread producer([&] () {
        for (int i = 0; i < N; i++) {
            buf.Push(i);
        }
        buf.Push(-1);
    });

    int counter = 0;
    std::thread consumer([&] () {
        int value = 0;
        do {
            buf.Get(&value);
            counter++;

            if (value >= 0) {
                EXPECT_EQ(value, counter - 1);
            }
        } while (value >= 0);
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(N + 1, counter);
}

TEST(LockFreeRingBufferTest, DISABLED_MutliByOneProducerConsumer) {
    LockFreeRingBuffer<int> buf(1U << 4);

    const int N = 1000;

    std::thread producers[4];
    int i = arraysize(producers);
    while (i--) {
        producers[i] = std::move(std::thread([&] (int id) {
            for (int j = id * N; j < (id + 1) * N; j++) {
                buf.Push(j);
            }
        }, i));
    }

    int counter = 0;
    std::thread consumer([&] () {
        int value = 0;
        do {
            buf.Get(&value);
            counter++;
        } while (value >= 0);
    });

    i = arraysize(producers);
    while (i--) {
        producers[i].join();
    }

    buf.Push(-1);
    consumer.join();

    ASSERT_EQ(arraysize(producers) * N + 1, counter);
}

} // namespace yukino