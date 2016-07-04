#ifndef YUKINO_RW_SPIN_LOCK_H_
#define YUKINO_RW_SPIN_LOCK_H_

#include "glog/logging.h"
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

    void ReadLock();
    bool TryReadLock();
    void WriteLock();
    bool TryWriteLock();

    void Unlock();

    int num_readers() { return kLockBais - spin_lock_.load(); }
    int num_writers() { return spin_lock_.load() == kLockBais ? 0 : 1; }

    const std::atomic<int32_t> &TEST_spin_lock() { return spin_lock_; }
private:
    std::atomic<int32_t> spin_lock_;
};

class ReaderLock {
public:
    ReaderLock(RWSpinLock *lock)
        : lock_(lock) {
        lock_->ReadLock();
    }

    ~ReaderLock() { lock_->Unlock(); }

private:
    RWSpinLock *lock_;
};

class WriterLock {
public:
    WriterLock(RWSpinLock *lock)
        : lock_(lock) {
        lock_->WriteLock();
    }

    ~WriterLock() { lock_->Unlock(); }
    
private:
    RWSpinLock *lock_;
};

static_assert(sizeof(RWSpinLock) == 4, "RWSpinLock too large");

} // namespace yukino

#endif // YUKINO_RW_SPIN_LOCK_H_