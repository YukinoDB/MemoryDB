#ifndef YUKINO_COCURRENT_HASH_MAP_H_
#define YUKINO_COCURRENT_HASH_MAP_H_

#include "rw_spin_lock.h"
#include "yuki/slice.h"

namespace yukino {

struct Obj;
struct KeyBoundle;

class CocurrentHashMap {
public:
    struct Node {
        KeyBoundle *key;
        Obj        *value;
        Node       *next;
    };

    struct Slot {
        RWSpinLock rwlock;
        Node       node;
    };

    CocurrentHashMap(int initial_size);
    ~CocurrentHashMap();

    Node *FindOrMakeRoom(yuki::SliceRef key, uint64_t version_number);

private:
    Slot *slots_;
    int   num_slots_;
};

} // namespace yukino

#endif // YUKINO_COCURRENT_HASH_MAP_H_