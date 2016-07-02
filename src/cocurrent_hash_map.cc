#include "cocurrent_hash_map.h"
#include "key.h"

namespace yukino {

/*static*/ unsigned int CocurrentHashMap::SDBMHash(const char *p, size_t n) {
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
    , balance_fator_(0.8f)
    , balance_fator_down_(0.1f) {
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
}

yuki::Status CocurrentHashMap::Put(yuki::SliceRef key, uint64_t version_number,
                                   Obj *value) {
    auto slot = Take(key);

    ReaderLock gaint(&gaint_lock_);
    WriterLock scope(&slot->rwlock);
    auto node = UnsafeFindOrMakeRoom(key, slot);
    if (!node) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory.");
    }
    if (!node->key) {
        node->key = MakeKeyBoundle(key, 0, version_number);
    }
    if (!node->key) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory.");
    }
//    if (node->value != value) {
//        ObjRelease(node->value);
//    }
    node->value = value;
    return yuki::Status::OK();
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
        *value = node->value;
    }
    return yuki::Status::OK();
}

CocurrentHashMap::Node *
CocurrentHashMap::UnsafeFindOrMakeRoom(yuki::SliceRef key, Slot *slot) {
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
        node = new Node;
        if (!node) {
            return nullptr;
        }
        p->next = node;
        node->key   = nullptr;
        node->value = nullptr;
        node->next  = nullptr;
        auto num_keys = std::atomic_fetch_add_explicit(&num_keys_, 1,
                                                     std::memory_order_release);
        gaint_lock_.ReadUnlock();
        ExtendIfNeed(num_keys);
        gaint_lock_.ReadLock(RWSpinLock::kDefaultSpinCount);
    }
    return node;
}

void CocurrentHashMap::ExtendIfNeed(int num_keys) {
    if (static_cast<float>(num_keys) / static_cast<float>(num_slots_) <=
        balance_fator_) {
        return;
    }

    DCHECK_GT(balance_fator_, balance_fator_down_);
    WriterLock lock(&gaint_lock_);
    // num_keys / num_slots = mid_fator;
    // num_keys = mid_fator * num_slots;
    auto new_num_slots = static_cast<int>(num_keys /
                                          (balance_fator_down_ +
                                           (balance_fator_ -
                                            balance_fator_down_) / 2));
    auto new_slots = new Slot[new_num_slots];
    if (!new_slots) {
        return;
    }
    InitSlots(new_slots, new_num_slots);
    Rehash(slots_, num_slots_, new_slots, new_num_slots);
    delete[] slots_;

    slots_     = new_slots;
    num_slots_ = new_num_slots;
}

void CocurrentHashMap::Rehash(Slot *from, int num_from, Slot *to, int num_to) {
    for (int i = 0; i < num_from; i++) {
        auto slot = &from[i];

        while (slot->node) {
            auto node = slot->node;
            slot->node = node->next;

            auto key = node->key->key();
            auto to_slot_index = (SDBMHash(key.Data(), key.Length()) | 1) %
                num_to;
            auto to_slot = &to[to_slot_index];

            node->next = to_slot->node;
            to_slot->node = node;
        }
    }
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