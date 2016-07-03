#ifndef YUKINO_OBJ_H_
#define YUKINO_OBJ_H_

#include "yuki/slice.h"
#include "yuki/varint.h"
#include "glog/logging.h"
#include <atomic>
#include <stdint.h>
#include <stdlib.h>

namespace yukino {

enum ObjTy: uint8_t {
    YKN_STRING,
    YKN_INTEGER,
};

struct Obj {
    std::atomic<int32_t> ref_count;
    uint8_t raw;

    Obj(ObjTy ty) : ref_count(0), raw(ty) {}
    Obj() = delete;
    Obj(const Obj &) = delete;
    Obj(Obj &&) = delete;

    void AddRef() { ref_count.fetch_add(1); }
    inline void Release();

    ObjTy type() const { return static_cast<ObjTy>(raw); }

    const uint8_t *payload() const { return &raw + 1; }
};

class String : public Obj {
public:
    inline uint32_t size() const;
    inline const char *buf() const;

    inline char *mutable_buf();

    inline yuki::Slice data() const;

    static inline size_t PredictSize(yuki::SliceRef s);
    static inline String *Build(yuki::SliceRef s, void *buf, size_t size);
    static inline String *New(yuki::SliceRef s);
};

class Integer : public Obj {
public:
    inline int64_t data() const;

    static inline size_t PredictSize(int64_t i);
    static inline Integer *Build(int64_t i, void *buf, size_t size);
    static inline Integer *New(int64_t i);
};

static_assert(sizeof(Obj) == sizeof(String), "Fixed String size.");
static_assert(sizeof(Obj) == sizeof(Integer), "Fixed Integer size.");

void ObjRelease(Obj *ob);

inline Obj *ObjAddRef(Obj *ob) {
    if (ob) {
        ob->AddRef();
    }
    return ob;
}

inline void Obj::Release() {
    if (ref_count.fetch_and(1) == 0) {
        free(this);
    }
}

inline uint32_t String::size() const {
    auto p = payload();
    size_t len;
    return yuki::Varint::Decode32(p, &len);
}

inline const char *String::buf() const {
    auto p = &raw + 1;
    size_t len;
    yuki::Varint::Decode32(p, &len);
    return reinterpret_cast<const char *>(p + len);
}

inline char *String::mutable_buf() {
    auto p = &raw + 1;
    size_t len;
    yuki::Varint::Decode32(p, &len);
    return reinterpret_cast<char *>(p + len);
}

inline yuki::Slice String::data() const {
    auto p = &raw + 1;
    size_t len;
    auto size = yuki::Varint::Decode32(p, &len);
    return yuki::Slice(reinterpret_cast<const char *>(p + len), size);
}

/*static*/
inline size_t String::PredictSize(yuki::SliceRef s) {
    return yuki::Varint::Sizeof32(static_cast<uint32_t>(s.Length())) +
        s.Length() + sizeof(String);
}

/*static*/
inline String *String::Build(yuki::SliceRef s, void *buf, size_t size) {
    if (size < PredictSize(s)) {
        return nullptr;
    }
    auto base = new (buf) Obj(YKN_STRING);
    auto raw  = &base->raw + 1;
    raw += yuki::Varint::Encode32(static_cast<uint32_t>(s.Length()), raw);
    memcpy(raw, s.Data(), s.Length());
    base->AddRef();
    return static_cast<String *>(base);
}

/*static*/
inline String *String::New(yuki::SliceRef s) {
    auto size = PredictSize(s);
    auto buf  = malloc(size);
    return Build(s, buf, size);
}

inline int64_t Integer::data() const {
    size_t len;
    auto rv = yuki::Varint::DecodeS64(&raw + 1, &len);
    DCHECK_LE(len, yuki::Varint::kMax64Len);
    return rv;
}

/*static*/
inline size_t Integer::PredictSize(int64_t i) {
    return yuki::Varint::Sizeof64(yuki::ZigZag::Encode64(i)) +
        sizeof(Integer);
}

/*static*/
inline Integer *Integer::Build(int64_t i, void *buf, size_t size) {
    if (size < PredictSize(i)) {
        return nullptr;
    }
    auto base = new (buf) Obj(YKN_INTEGER);
    yuki::Varint::EncodeS64(i, &base->raw + 1);
    base->AddRef();
    return static_cast<Integer *>(base);
}

/*static*/
inline Integer *Integer::New(int64_t i) {
    auto size = PredictSize(i);
    auto buf  = malloc(size);
    return Build(i, buf, size);
}

} // namespace yukino

#endif // YUKINO_OBJ_H_