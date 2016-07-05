#ifndef YUKINO_ITERATOR_H_
#define YUKINO_ITERATOR_H_

#include "yuki/status.h"

namespace yukino {

struct KeyBoundle;
struct Obj;

class Iterator {
public:
    Iterator();
    Iterator(const Iterator &) = delete;
    Iterator(Iterator &&) = delete;
    void operator = (const Iterator &) = delete;

    virtual ~Iterator();

    virtual bool Valid() const = 0;

    virtual void SeekToFirst() = 0;

    virtual void Next() = 0;

    virtual yuki::Status status() const = 0;

    virtual KeyBoundle *key() const = 0;

    virtual Obj *value() const = 0;
}; // class Iterator

} // namespace yukino

#endif // YUKINO_ITERATOR_H_