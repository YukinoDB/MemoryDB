#ifndef YUKINO_RW_SPIN_LOCK_H_
#define YUKINO_RW_SPIN_LOCK_H_

#include <stdint.h>
#include <atomic>
#include <thread>

namespace yukino {

class RWSpinLock {
public:
    static const int32_t kLockBais;
    static const int     kDefaultSpinCount;

    RWSpinLock()
        : spin_lock_(kLockBais) {}

    inline void ReadLock(int spin_count);
    inline void ReadUnlock();

    inline void WriteLock(int spin_count);
    inline void WriteUnlock();

    const std::atomic<int32_t> &TEST_spin_lock() { return spin_lock_; }
private:
    std::atomic<int32_t> spin_lock_;
};

class ReaderLock {
public:
    ReaderLock(RWSpinLock *lock)
        : ReaderLock(lock, RWSpinLock::kDefaultSpinCount) {}

    ReaderLock(RWSpinLock *lock, int spin_count)
        : lock_(lock) {
        lock_->ReadLock(spin_count);
    }

    ~ReaderLock() { lock_->ReadUnlock(); }

private:
    RWSpinLock *lock_;
};

class WriterLock {
public:
    WriterLock(RWSpinLock *lock)
        : WriterLock(lock, RWSpinLock::kDefaultSpinCount) {}

    WriterLock(RWSpinLock *lock, int spin_count)
        : lock_(lock) {
        lock_->WriteLock(spin_count);
    }

    ~WriterLock() { lock_->WriteUnlock(); }
    
private:
    RWSpinLock *lock_;
};

static_assert(sizeof(RWSpinLock) == 4, "RWSpinLock too large");

inline void RWSpinLock::ReadLock(int spin_count) {
    while (std::atomic_load_explicit(&spin_lock_,
                                     std::memory_order_acquire) <= 0) {
        if (spin_count <= 0) {
            std::this_thread::yield();
        } else {
            spin_count--;
        }
    }
    std::atomic_fetch_sub_explicit(&spin_lock_, 1, std::memory_order_release);
}

inline void RWSpinLock::ReadUnlock() {
    std::atomic_fetch_add_explicit(&spin_lock_, 1, std::memory_order_release);
}

inline void RWSpinLock::WriteLock(int spin_count) {
    while (std::atomic_load_explicit(&spin_lock_,
                                     std::memory_order_acquire) != kLockBais) {
        if (spin_count <= 0) {
            std::this_thread::yield();
        } else {
            spin_count--;
        }
    }
    std::atomic_fetch_sub_explicit(&spin_lock_, kLockBais,
                                   std::memory_order_release);
}

inline void RWSpinLock::WriteUnlock() {
    std::atomic_fetch_add_explicit(&spin_lock_, kLockBais,
                                   std::memory_order_release);
}

} // namespace yukino

#endif // YUKINO_RW_SPIN_LOCK_H_