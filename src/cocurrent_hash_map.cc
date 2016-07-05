#include "cocurrent_hash_map.h"
#include "iterator.h"
#include "key.h"
#include "obj.h"

namespace yukino {

namespace {

class IteratorImpl : public Iterator {
public:
    typedef CocurrentHashMap::Slot Slot;
    typedef CocurrentHashMap::Node Node;

    IteratorImpl(RWSpinLock *rwlock, Slot *begin, Slot *end)
        : rwlock_(DCHECK_NOTNULL(rwlock))
        , begin_(DCHECK_NOTNULL(begin))
        , end_(DCHECK_NOTNULL(end)) {
        DCHECK_NE(begin, end);

        rwlock_->ReadLock();
    }

    virtual ~IteratorImpl() override;
    virtual bool Valid() const override;
    virtual void SeekToFirst() override;
    virtual void Next() override;
    virtual yuki::Status status() const override;
    virtual KeyBoundle *key() const override;
    virtual Obj *value() const override;

private:
    RWSpinLock *rwlock_;
    Slot *begin_;
    Slot *end_;
    Node *node_ = nullptr;
    Slot *now_ = nullptr;
};

IteratorImpl::~IteratorImpl() {
    DCHECK_NOTNULL(rwlock_)->Unlock();
}

bool IteratorImpl::Valid() const {
    return node_ != nullptr;
}

void IteratorImpl::SeekToFirst() {
    for (now_ = begin_; now_ < end_; now_++) {
        if (now_->node) {
            node_ = now_->node;
            break;
        }
    }
}

void IteratorImpl::Next() {
    DCHECK(Valid());

    node_ = node_->next;
    if (!node_) {
        while (now_++ < end_) {
            if (now_->node) {
                node_ = now_->node;
                break;
            }
        }
    }
}

yuki::Status IteratorImpl::status() const {
    return yuki::Status::OK();
}

KeyBoundle *IteratorImpl::key() const {
    DCHECK(Valid());
    return DCHECK_NOTNULL(node_->key);
}

Obj *IteratorImpl::value() const {
    DCHECK(Valid());
    return DCHECK_NOTNULL(node_->value);
}

} // namespace

/*static*/ unsigned int CocurrentHashMap::Hash(const char *p, size_t n) {
    // JSHash
    unsigned int hash = 1315423911;
    for (size_t i = 0; i < n; i++) {
        hash ^= ((hash << 5) + (*p++) + (hash >> 2));
    }
    return (hash & 0x7FFFFFFF);
}

CocurrentHashMap::CocurrentHashMap(int initial_size)
    : slots_(nullptr)
    , num_slots_(0)
    , min_num_slots_(initial_size)
    , num_keys_(0)
    , rehash_(0)
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
    delete[] slots_;
}

yuki::Status CocurrentHashMap::Put(yuki::SliceRef key, uint64_t version_number,
                                   Obj *value) {
    auto num_keys = std::atomic_load_explicit(&num_keys_,
                                              std::memory_order_acquire);
    ExtendIfNeed(num_keys + 1);

    ReaderLock gaint(&gaint_lock_);
    auto slot = Take(key);

    WriterLock scope(&slot->rwlock);
    auto node = UnsafeFindOrMakeRoom(key, slot);
    if (!node) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory.");
    }


    if (!DCHECK_NOTNULL(node)->key) {
        node->key = MakeKeyBoundle(key, 0, version_number);
    }
    if (!node->key) {
        return yuki::Status::Errorf(yuki::Status::kCorruption,
                                    "not enough memory.");
    }
    if (node->value != value) {
        ObjRelease(node->value);
        node->value = ObjAddRef(value);
    }
    return yuki::Status::OK();
}

bool CocurrentHashMap::Delete(yuki::SliceRef key) {
    auto num_keys = std::atomic_load_explicit(&num_keys_,
                                              std::memory_order_acquire);
    ShrinkIfNeed(num_keys - 1);

    ReaderLock gaint(&gaint_lock_);

    auto slot = Take(key);
    WriterLock scope(&slot->rwlock);

    return UnsafeDeleteRoom(key, slot);
}

yuki::Status CocurrentHashMap::Get(yuki::SliceRef key, Version *ver,
                                   Obj **value) {
    ReaderLock gaint(&gaint_lock_);
    auto slot = Take(key);

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

yuki::Status
CocurrentHashMap::Exec(yuki::SliceRef key,
                       std::function<void (const Version &, Obj *)> proc) {
    ReaderLock gaint(&gaint_lock_);
    auto slot = Take(key);

    ReaderLock scope(&slot->rwlock);
    auto node = UnsafeFindRoom(key, slot);
    if (!node) {
        return yuki::Status::Errorf(yuki::Status::kNotFound, "key not found.");
    }

    proc(node->key->version(), ObjAddRef(node->value));
    ObjRelease(node->value);

    return yuki::Status::OK();
}

Iterator *CocurrentHashMap::iterator() {
    return new IteratorImpl(&gaint_lock_, slots_, slots_ + num_slots_);
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
        memset(node, 0, sizeof(*node));

        std::atomic_fetch_add_explicit(&num_keys_, 1,
                                       std::memory_order_release);
    }
    slot->node = stub.next;
    return node;
}

bool CocurrentHashMap::UnsafeDeleteRoom(yuki::SliceRef key, Slot *slot) {
    Node stub;
    stub.key  = nullptr;
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
        free(node->key);
        ObjRelease(node->value);
        delete node;

        std::atomic_fetch_sub_explicit(&num_keys_, 1,
                                       std::memory_order_release);
    }
    slot->node = stub.next;
    return !!node;
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

bool CocurrentHashMap::ExtendIfNeed(int num_keys) {
    gaint_lock_.ReadLock();
    auto key_rate = static_cast<float>(num_keys) / static_cast<float>(num_slots_);
    if (key_rate <= balance_fator_) {
        gaint_lock_.Unlock();
        return false;
    }
    gaint_lock_.Unlock();

    return ResizeSlots(num_keys);
}

bool CocurrentHashMap::ShrinkIfNeed(int num_keys) {
    gaint_lock_.ReadLock();
    auto key_rate = static_cast<float>(num_keys) / static_cast<float>(num_slots_);
    if (key_rate >= balance_fator_down_) {
        gaint_lock_.Unlock();
        return false;
    }
    if (num_slots_ == min_num_slots_) {
        gaint_lock_.Unlock();
        return false;
    }
    gaint_lock_.Unlock();
    
    return ResizeSlots(num_keys);
}

bool CocurrentHashMap::ResizeSlots(int num_keys) {
    DCHECK_GT(balance_fator_, balance_fator_down_);

    WriterLock gaint(&gaint_lock_);
    if (static_cast<float>(num_keys) / static_cast<float>(num_slots_) >=
        balance_fator_down_ &&
        static_cast<float>(num_keys) / static_cast<float>(num_slots_) <=
        balance_fator_) {

        return true;
    }

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