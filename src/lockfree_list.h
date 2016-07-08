#ifndef YUKINO_LOCKFREE_LIST_H_
#define YUKINO_LOCKFREE_LIST_H_

#include <atomic>
#include <stdio.h>

namespace yukino {

template<class T>
struct ValueManaged {
    void Grab(T value) {}
    void Drop(T value) {}
};

template<class T, class M = ValueManaged<T>>
class LockFreeList {
public:
    struct Node {
        T value;
        std::atomic<Node *> next;

        Node() : value(0), next(nullptr) {}
    };

    LockFreeList() : LockFreeList(M()) {}
    inline LockFreeList(M managed);

    LockFreeList(const LockFreeList &) = delete;
    LockFreeList(LockFreeList &&) = delete;
    void operator = (const LockFreeList &) = delete;

    inline ~LockFreeList();

    inline void InsertTail(T value);
    inline void InsertHead(T value);

    inline bool PopHead(T *value);

    inline void Delete(int index);

    inline Node *Take(int index, Node **left);

    Node *begin() const { return head_->next.load(); }
    Node *end() const   { return tail_; }

    inline size_t size() const;

    bool empty() const { return head_->next.load() != nullptr; }

//    inline bool IsMarked(Node *ref);
//    inline Node *GetMarked(Node *ref);
//    inline Node *GetUnmarked(Node *ref);

private:
    Node *head_;
    Node *tail_;
    M managed_;
};

template<class T, class M>
inline LockFreeList<T, M>::LockFreeList(M managed)
    : head_(new Node)
    , tail_(new Node)
    , managed_(managed) {
    head_->next.store(tail_);
}

template<class T, class M>
inline LockFreeList<T, M>::~LockFreeList() {
    auto node = head_->next.load();
    while (node != tail_) {
        auto tmp = node;
        node = node->next.load();

        managed_.Drop(tmp->value);
        delete tmp;
    }

    delete head_;
    delete tail_;
}

template<class T, class M>
inline void LockFreeList<T, M>::InsertHead(T value) {
    auto new_node = new Node;
    new_node->value = value;
    managed_.Grab(new_node->value);

    Node *right;
    do {
        right = head_->next.load();
        new_node->next.store(right);
    } while (!std::atomic_compare_exchange_strong(&head_->next, &right,
                                                  new_node));
}

template<class T, class M>
inline void LockFreeList<T, M>::InsertTail(T value) {
    auto new_node = new Node;
    new_node->value = value;
    managed_.Grab(new_node->value);

    Node *left, *node;
    do {
        node = Take(-1, &left);
        new_node->next.store(node);
    } while (!std::atomic_compare_exchange_strong(&left->next, &node,
                                                  new_node));
}

template<class T, class M>
inline bool LockFreeList<T, M>::PopHead(T *value) {

    Node *node, *right;
    do {
        node = head_->next.load();
        if (node == tail_) {
            return false;
        }
        right = node->next.load();
    } while (!std::atomic_compare_exchange_strong(&head_->next, &node, right));

    *value = node->value;
    managed_.Grab(node->value);
    delete node;
    return true;
}

template<class T, class M>
inline void LockFreeList<T, M>::Delete(int index) {
    Node *left, *right, *node = nullptr;

    do {
        node = Take(index, &left);
        if (node == tail_) {
            return;
        }
        right = node->next.load();
    } while (!std::atomic_compare_exchange_strong(&left->next, &node, right));

    managed_.Drop(node->value);
    delete node;
}

template<class T, class M>
inline typename LockFreeList<T, M>::Node *
LockFreeList<T, M>::Take(int index, Node **left) {
take_again:
    Node *prev = head_;
    Node *node = head_->next.load();
    if (index >= 0) {
        while (index--) {
            prev = node;
            node = node->next.load();
            if (node == tail_) {
                break;
            }
        }
    } else {
        while (node != tail_) {
            prev = node;
            node = node->next.load();
        }
    }
    if (left) {
        *left = prev;
    }
    return node;
}

//template<class T, class M>
//inline bool LockFreeList<T, M>::IsMarked(Node *ref) {
//    return (static_cast<uintptr_t>(ref) & 1UL) != 0;
//}
//
//template<class T>
//inline typename LockFreeList<T>::Node *
//LockFreeList<T>::GetMarked(Node *ref) {
//    return static_cast<Node *>(reinterpret_cast<uintptr_t>(ref) | 1UL);
//}
//
//template<class T>
//inline typename LockFreeList<T>::Node *
//LockFreeList<T>::GetUnmarked(Node *ref) {
//    static const uintptr_t mask = ~static_cast<uintptr_t>(1);
//    return static_cast<Node *>(reinterpret_cast<uintptr_t>(ref) & mask);
//}

template<class T, class M>
inline size_t LockFreeList<T, M>::size() const {
    size_t n = 0;
    for (auto node = begin(); node != end(); node = node->next.load()) {
        n++;
    }
    return n;
}

} // namespace yukino

#endif // YUKINO_LOCKFREE_LIST_H_