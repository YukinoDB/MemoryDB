#include "rw_spin_lock.h"
#include "yuki/utils.h"
#include "gtest/gtest.h"
#include <thread>

namespace yukino {

TEST(RWSpinLockTest, Sanity) {
    RWSpinLock lock;
    EXPECT_EQ(RWSpinLock::kLockBais, lock.TEST_spin_lock().load());

    lock.ReadLock();
    EXPECT_EQ(RWSpinLock::kLockBais - 1, lock.TEST_spin_lock().load());

    lock.ReadLock();
    EXPECT_EQ(RWSpinLock::kLockBais - 2, lock.TEST_spin_lock().load());

    lock.Unlock();
    EXPECT_EQ(RWSpinLock::kLockBais - 1, lock.TEST_spin_lock().load());

    lock.Unlock();
    EXPECT_EQ(RWSpinLock::kLockBais, lock.TEST_spin_lock().load());

    lock.WriteLock();
    EXPECT_EQ(0, lock.TEST_spin_lock().load());

    lock.Unlock();
    EXPECT_EQ(RWSpinLock::kLockBais, lock.TEST_spin_lock().load());
}

TEST(RWSpinLockTest, MutliReader) {
    std::thread readers[8];
    int result[8] = {0};
    std::atomic<int> counter(0);
    RWSpinLock rwlock;
    static const int kN = 100000;

    for (int i = 0; i < arraysize(readers); i++) {
        readers[i] = std::thread([&] (int id) {
            while (counter.load() < kN) {
                rwlock.ReadLock();
                if (counter.load() % 7) {
                    result[id]++;
                }
                rwlock.Unlock();
            }
        }, i);
    }
    for (int i = 0; i < kN; i++) {
        rwlock.WriteLock();
        counter.fetch_add(1);
        rwlock.Unlock();
    }
    for (int i = 0; i < arraysize(readers); i++) {
        readers[i].join();
    }
}

// if (obj != expect) {
//     return false;
// }
// expect
//
TEST(RWSpinLockTest, CASExample) {
    std::atomic<int> flag(1);
    int expect = 0;
    EXPECT_FALSE(std::atomic_compare_exchange_weak(&flag, &expect, 0));
    EXPECT_EQ(1, expect);
    EXPECT_EQ(1, flag.load());

    expect = 1;
    EXPECT_TRUE(std::atomic_compare_exchange_weak(&flag, &expect, 2));
    EXPECT_EQ(1, expect);
    EXPECT_EQ(2, flag.load());
}

} // namespace yukino