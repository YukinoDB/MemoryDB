#ifndef YUKINO_KEY_H_
#define YUKINO_KEY_H_

#include "glog/logging.h"
#include "yuki/slice.h"
#include "yuki/varint.h"
#include <stdint.h>

namespace yukino {

struct Version {
    uint64_t type  :  8;
    uint64_t number: 56;
};

//
// Key Boundle:
// [key-length(varint32)][key bytes][version(varint32)]
//
//typedef char *KeyBoundle;

struct KeyBoundle {
    uint8_t raw;

    inline yuki::Slice key() const;
    inline uint32_t key_size() const;
    Version version() const;

    static KeyBoundle *Build(yuki::SliceRef key, uint8_t type,
                             uint64_t version_number, void *bytes,
                             size_t bytes_size);

    static
    inline size_t PredictBoundleSize(yuki::SliceRef key, uint64_t version_number);
};

static_assert(sizeof(KeyBoundle) == 1, "Key Boundle size must be 1 byte.");
static_assert(sizeof(Version) == 8, "Version size must be 8 byte.");

inline uint32_t KeyBoundle::key_size() const {
    size_t len = 0;
    auto size = yuki::Varint::Decode32(&raw, &len);
    DCHECK_LE(len, yuki::Varint::kMax32Len);
    return size;
}

inline yuki::Slice KeyBoundle::key() const {
    size_t len = 0;
    auto size = yuki::Varint::Decode32(&raw, &len);
    DCHECK_LE(len, yuki::Varint::kMax32Len);
    auto key_ptr = reinterpret_cast<const char *>(&raw) + len;
    return yuki::Slice(key_ptr, size);
}

/*static*/
inline
size_t KeyBoundle::PredictBoundleSize(yuki::SliceRef key,
                                      uint64_t version_number) {
    size_t size = yuki::Varint::Sizeof32(static_cast<uint32_t>(key.Length()));
    size += key.Length();
    size += 1; // type
    size += yuki::Varint::Sizeof64(version_number);
    return size;
}

} // namespace yukino

#endif // YUKINO_KEY_H_