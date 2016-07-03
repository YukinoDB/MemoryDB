#include "cocurrent_hash_map.h"
#include "key.h"
#include "obj.h"

namespace yukino {

/*static*/ unsigned int CocurrentHashMap::Hash(const char *p, size_t n) {
    // SDM hash
    unsigned int hash = 0;
    for (int i = 0; i < n; i++) {
        // equivalent to: hash = 65599 * hash + (*str++);
        hash = (*p++) + (hash << 6) + (hash << 16) - hash;
    }
    return (hash & 0x7FFFFFFF);
}

CocurrentHashMap::CocurrentHashMap(int initial_size)
    : slots_(nullptr)
    , num_slots_(0)
    , min_num_slots_(initial_size)
    , num_keys_(0)
    , balance_fator_(0.9f)
    , balance_fator_down_(0.2f) {
    if (initial_size <= 0) {
        return;
    }

    slots_ = new Slot[initial_size];
    if (slots_) {
        InitSlots(slots_, initial_size);
        num_slots_ = initial_size;
    }
}

CocurrentHashMap::~CocurrentHashMap() {
    for (int i = 0; i < num_slots_; i++) {
        auto slot = &slots_[i];

        while (slot->node) {
            auto node = slot->node;
            slot->node = node->next;

            free(node->key);
            ObjRelease(node->value);
            delete node;
        }
    }
}

yuki::Status CocurrentHashMap::Put(yuki::SliceRef key, uint64_t version_number,
                                   Obj *value) {
    ReaderLock gaint(&gaint_lock_);

    Node *node = nullptr;
    Slot *slot = nullptr;
    bool rehash = false;
    do {
        slot = Take(key);
        slot->rwlock.WriteLock(RWSpinLock::kDefaultSpinCount);
        
        node = UnsafeFindOrMakeRoom(key, slot, &rehash);
        if (!node && !rehash) {
            return yuki::Status::Errorf(yuki::Status::kCorruption,
                                        "not enough memory.");
        }
    } while (rehash);

    if (!node->key) {
        node->key = MakeKeyBoundle(key, 0, version_number);
    }
    if (!node->key) {
        slot->rwlock.WriteUnlock();
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory.");
    }
    if (node->value != value) {
        ObjRelease(node->value);
        node->value = ObjAddRef(value);
    }
    slot->rwlock.WriteUnlock();
    return yuki::Status::OK();
}

bool CocurrentHashMap::Delete(yuki::SliceRef key) {
    auto slot = Take(key);

    ReaderLock gaint(&gaint_lock_);
    WriterLock scope(&slot->rwlock);

    bool rehash = false;
    return UnsafeDeleteRoom(key, slot, &rehash);
}

yuki::Status CocurrentHashMap::Get(yuki::SliceRef key, Version *ver,
                                   Obj **value) {
    auto slot = Take(key);

    ReaderLock gaint(&gaint_lock_);
    ReaderLock scope(&slot->rwlock);
    auto node = UnsafeFindRoom(key, slot);
    if (!node) {
        return yuki::Status::Errorf(yuki::Status::kNotFound, "key not found.");
    }

    if (ver) {
        *ver = node->key->version();
    }
    if (value) {
        *value = ObjAddRef(node->value);
    }
    return yuki::Status::OK();
}

CocurrentHashMap::Node *
CocurrentHashMap::UnsafeFindOrMakeRoom(yuki::SliceRef key, Slot *slot,
                                       bool *rehash) {
    Node stub;
    stub.next = slot->node;
    auto p = &stub;
    auto node = slot->node;
    while (node) {
        if (DCHECK_NOTNULL(node->key)->key().Compare(key) == 0) {
            break;
        }
        p = node;
        node = node->next;
    }
    if (!node) {
        auto num_keys = std::atomic_fetch_add_explicit(&num_keys_, 1,
                                                       std::memory_order_release);
        *rehash = ExtendIfNeed(num_keys);
        if (*rehash) {
            std::atomic_fetch_sub_explicit(&num_keys_, 1,
                                           std::memory_order_release);
            return nullptr;
        }

        node = new Node;
        if (!node) {
            return nullptr;
        }
        p->next = node;
        node->key   = nullptr;
        node->value = nullptr;
        node->next  = nullptr;
    }
    slot->node = stub.next;
    return node;
}

bool CocurrentHashMap::UnsafeDeleteRoom(yuki::SliceRef key, Slot *slot,
                                        bool *rehash) {
    Node stub;
    stub.next = slot->node;
    auto p = &stub;
    auto node = slot->node;
    while (node) {
        if (DCHECK_NOTNULL(node->key)->key().Compare(key) == 0) {
            break;
        }
        p = node;
        node = node->next;
    }
    if (node) {
        p->next = node->next;
        free(p->key);
        ObjRelease(node->value);
        delete node;

        auto num_keys = std::atomic_fetch_sub_explicit(&num_keys_, 1,
                                                       std::memory_order_release);
        *rehash = ShrinkIfNeed(num_keys);
        return true;
    }
    return false;
}

CocurrentHashMap::Node *
CocurrentHashMap::UnsafeFindRoom(yuki::SliceRef key, Slot *slot) {
    auto node = slot->node;
    while (node) {
        if (DCHECK_NOTNULL(node->key)->key().Compare(key) == 0) {
            break;
        }
        node = node->next;
    }
    return node;
}

bool CocurrentHashMap::ResizeSlots(int num_keys) {
    DCHECK_GT(balance_fator_, balance_fator_down_);
    WriterLock lock(&gaint_lock_);
    // fator mid down
    //   ^--->
    // num_keys / num_slots = mid_fator;
    // num_keys = mid_fator * num_slots;
    auto new_num_slots = static_cast<int>(num_keys /
                                          (balance_fator_down_ +
                                           (balance_fator_ -
                                            balance_fator_down_) / 2));
    if (new_num_slots < min_num_slots_) {
        new_num_slots = min_num_slots_;
    }
    auto new_slots = new Slot[new_num_slots];
    if (!new_slots) {
        return false;
    }
    InitSlots(new_slots, new_num_slots);
    Rehash(slots_, num_slots_, new_slots, new_num_slots);
    delete[] slots_;

    slots_     = new_slots;
    num_slots_ = new_num_slots;
    return true;
}

void CocurrentHashMap::Rehash(Slot *from, int num_from, Slot *to, int num_to) {
    for (int i = 0; i < num_from; i++) {
        auto from_slot = &from[i];

        while (from_slot->node) {
            auto node = from_slot->node;
            from_slot->node = node->next;

            auto key = DCHECK_NOTNULL(node->key)->key();
            auto to_slot_index = (Hash(key.Data(), key.Length()) | 1) %
                num_to;
            auto to_slot = &to[to_slot_index];

            node->next = to_slot->node;
            to_slot->node = node;
        }
    }
}

KeyBoundle *CocurrentHashMap::MakeKeyBoundle(yuki::SliceRef key, uint8_t type,
                                             uint64_t version_number) {
    auto key_boundle_size = KeyBoundle::PredictBoundleSize(key, version_number);
    auto key_buf = malloc(key_boundle_size);
    if (!key_buf) {
        return nullptr;
    }

    return KeyBoundle::Build(key, type, version_number, key_buf,
                             key_boundle_size);
}

} // namespace yukino