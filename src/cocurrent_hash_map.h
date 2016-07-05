#ifndef YUKINO_COCURRENT_HASH_MAP_H_
#define YUKINO_COCURRENT_HASH_MAP_H_

#include "rw_spin_lock.h"
#include "yuki/slice.h"
#include "yuki/status.h"
#include <atomic>
#include <string.h>
#include <pthread.h>

namespace yukino {

struct Obj;
struct KeyBoundle;
struct Version;
class Iterator;

class CocurrentHashMap {
public:
    struct Node {
        KeyBoundle *key;
        Obj        *value;
        Node       *next;
    };

    struct Slot {
        RWSpinLock rwlock;
        //pthread_rwlock_t rwlock;
        Node  *node;
    };

    CocurrentHashMap(int initial_size);
    ~CocurrentHashMap();

    yuki::Status Put(yuki::SliceRef key, uint64_t version_number, Obj *value);
    bool Delete(yuki::SliceRef key);

    yuki::Status Get(yuki::SliceRef key, Version *ver, Obj **value);
    inline bool Exist(yuki::SliceRef key);

    yuki::Status Exec(yuki::SliceRef key,
                      std::function<void (const Version &, Obj *)> proc);

    Iterator *iterator();

    Node *UnsafeFindOrMakeRoom(yuki::SliceRef key, Slot *slot);
    bool  UnsafeDeleteRoom(yuki::SliceRef key, Slot *slot);
    Node *UnsafeFindRoom(yuki::SliceRef key, Slot *slot);

    inline Slot *Take(yuki::SliceRef key);

    static unsigned int Hash(const char *p, size_t n);

    RWSpinLock *gaint_lock() { return &gaint_lock_; }

    float balance_fator() const { return balance_fator_; }
    float balance_fator_down() const { return balance_fator_down_; }
    int num_keys() const { return num_keys_; }
    int num_slots() const { return num_slots_; }


    // For testing:
    void TEST_ResizeSlots(int num_keys) { ResizeSlots(num_keys); }

private:
    inline void InitSlots(Slot *slots, int num_slots);

    inline bool ExtendIfNeed(int num_keys);
    inline bool ShrinkIfNeed(int num_keys);
    bool ResizeSlots(int num_keys);

    void Rehash(Slot *from, int num_from, Slot *to, int num_to);

    KeyBoundle *MakeKeyBoundle(yuki::SliceRef key, uint8_t type,
                               uint64_t version_number);

    Slot *slots_;
    int   num_slots_;
    const int min_num_slots_;
    float balance_fator_;
    float balance_fator_down_;
    std::atomic<int> num_keys_;
    std::atomic<int> rehash_;
    RWSpinLock gaint_lock_;
};

inline CocurrentHashMap::Slot *CocurrentHashMap::Take(yuki::SliceRef key) {
    auto slot_index = (Hash(key.Data(), key.Length()) | 1) % num_slots_;
    return &slots_[slot_index];
}

inline void CocurrentHashMap::InitSlots(Slot *slots, int num_slots) {
    for (int i = 0; i < num_slots; i++) {
        slots[i].node = nullptr;
    }
}

inline bool CocurrentHashMap::Exist(yuki::SliceRef key) {
    auto rv = Get(key, nullptr, nullptr);
    return rv.Ok();
}

} // namespace yukino

#endif // YUKINO_COCURRENT_HASH_MAP_H_