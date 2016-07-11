#ifndef YUKINO_HANDLE_H_
#define YUKINO_HANDLE_H_

namespace yukino {

template<class T>
class Handle {
public:
    Handle() : Handle(nullptr) {}

    explicit Handle(T *naked)
    : naked_(naked) {
        if (naked_) {
            naked_->AddRef();
        }
    }

    Handle(const Handle &other) : Handle(other.get()) {}

    Handle(Handle &&other)
    : naked_(other.naked_) {
        other.naked_ = nullptr;
    }

    ~Handle() {
        if (naked_) {
            naked_->Release();
        }
    }

    void Swap(Handle<T> *other) {
        auto tmp = naked_;
        naked_ = other->naked_;
        other->naked_ = tmp;
    }

    Handle<T> &operator = (const Handle<T> &other) {
        Handle(other.naked_).Swap(this);
        return *this;
    }

    Handle<T> &operator = (T *naked) {
        Reset(naked);
        return *this;
    }

    void Reset(T *naked) {
        Handle(naked).Swap(this);
    }

    T *Release() {
        auto tmp = naked_;
        naked_ = nullptr;
        return tmp;
    }

    T *operator -> () const { return DCHECK_NOTNULL(naked_); }
    
    T *get() const { return naked_; }
    
    T **address() { return &naked_; }
    
    int ref_count() { return naked_ ? naked_->RefCount(): 0; }
    
private:
    T *naked_;
}; // class Handle

} // namespace yukino

#endif // YUKINO_HANDLE_H_