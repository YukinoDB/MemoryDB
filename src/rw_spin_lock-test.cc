#include "rw_spin_lock.h"
#include "gtest/gtest.h"
#include <thread>

namespace yukino {

TEST(RWSpinLockTest, Sanity) {
    RWSpinLock lock;
    EXPECT_EQ(RWSpinLock::kLockBais, lock.TEST_spin_lock().load());

    lock.ReadLock(1);
    EXPECT_EQ(RWSpinLock::kLockBais - 1, lock.TEST_spin_lock().load());

    lock.ReadLock(1);
    EXPECT_EQ(RWSpinLock::kLockBais - 2, lock.TEST_spin_lock().load());

    lock.ReadUnlock();
    EXPECT_EQ(RWSpinLock::kLockBais - 1, lock.TEST_spin_lock().load());

    lock.ReadUnlock();
    EXPECT_EQ(RWSpinLock::kLockBais, lock.TEST_spin_lock().load());
}

TEST(RWSpinLockTest, MutliReader) {
    std::thread readers[8];
    int result[8] = {0};
    std::atomic<int> counter(0);
    RWSpinLock rwlock;
    static const int kN = 100000;

    for (int i = 0; i < 8; i++) {
        readers[i] = std::thread([&] (int id) {
            while (counter.load() < kN) {
                rwlock.ReadLock(100000);
                if (counter.load() % 7) {
                    result[id]++;
                }
                rwlock.ReadUnlock();
            }
        }, i);
    }
    for (int i = 0; i < kN; i++) {
        rwlock.WriteLock(100000);
        counter.fetch_add(1);
        rwlock.WriteUnlock();
    }
    for (int i = 0; i < 8; i++) {
        readers[i].join();
    }
}

} // namespace yukino