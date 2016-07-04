#include "rw_spin_lock.h"

namespace yukino {

/*static*/ const int32_t RWSpinLock::kLockBais = 0x010000000u;
/*static*/ const int     RWSpinLock::kDefaultSpinCount = 2 * 1024;

void RWSpinLock::ReadLock() {
    for (;;) {
        int readers = std::atomic_load_explicit(&spin_lock_,
                                                std::memory_order_relaxed);
        int expected = readers;

        if (readers > 0 &&
            std::atomic_compare_exchange_strong(&spin_lock_, &expected,
                                                readers - 1)) {
            return;
        }

        for (int n = 1; n < kDefaultSpinCount; n <<= 1) {
            for (int i = 0; i < n; i++) {
                // spin
                __asm__ ("pause");
            }

            readers = std::atomic_load_explicit(&spin_lock_,
                                                    std::memory_order_relaxed);
            expected = readers;
            if (readers > 0 &&
                std::atomic_compare_exchange_strong(&spin_lock_, &expected,
                                                    readers - 1)) {
                return;
            }
        }
        std::this_thread::yield();
    }
}

bool RWSpinLock::TryReadLock() {
    int readers = std::atomic_load_explicit(&spin_lock_,
                                            std::memory_order_relaxed);
    int expected = readers;

    if (readers > 0 &&
        std::atomic_compare_exchange_strong(&spin_lock_, &expected,
                                            readers - 1)) {
        return true;
    }
    return false;
}

void RWSpinLock::WriteLock() {
    for (;;) {
        int expected = kLockBais;
        if (std::atomic_load_explicit(&spin_lock_,
                                      std::memory_order_relaxed) == kLockBais &&
            std::atomic_compare_exchange_strong(&spin_lock_, &expected, 0)) {
            return;
        }

        for (int n = 1; n < kDefaultSpinCount; n <<= 1) {
            for (int i = 0; i < n; i++) {
                // spin
                __asm__ ("pause");
            }

            expected = kLockBais;
            if (std::atomic_load_explicit(&spin_lock_,
                                          std::memory_order_relaxed) == kLockBais &&
                std::atomic_compare_exchange_strong(&spin_lock_, &expected, 0)) {
                return;
            }
        }
        std::this_thread::yield();
    }
}

bool RWSpinLock::TryWriteLock() {
    int expected = kLockBais;
    if (std::atomic_load_explicit(&spin_lock_,
                                  std::memory_order_relaxed) == kLockBais &&
        std::atomic_compare_exchange_strong(&spin_lock_, &expected, 0)) {
        return true;
    }
    
    return false;
}

void RWSpinLock::Unlock() {
    int readers = std::atomic_load_explicit(&spin_lock_,
                                            std::memory_order_relaxed);
    if (readers == 0) {
        std::atomic_store_explicit(&spin_lock_, kLockBais,
                                   std::memory_order_relaxed);
        return;
    }

    for (;;) {
        int expected = readers;
        if (std::atomic_compare_exchange_strong(&spin_lock_, &expected,
                                              readers + 1)) {
            return;
        }
        readers = std::atomic_load_explicit(&spin_lock_,
                                            std::memory_order_relaxed);
    }
}

} // namespace yukino