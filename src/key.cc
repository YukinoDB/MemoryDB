#include "key.h"
#include "glog/logging.h"

namespace yukino {

Version KeyBoundle::version() const {
    auto key_slice = key();

    auto raw_buf = &raw;
    raw_buf += yuki::Varint::Sizeof32(static_cast<uint32_t>(key_slice.Length()));
    raw_buf += key_slice.Length();

    Version ver;
    ver.type = raw_buf[0];
    size_t len;
    ver.number = yuki::Varint::Decode64(raw_buf + 1, &len);
    DCHECK_LE(len, yuki::Varint::kMax64Len);
    return ver;
}

/*static*/ KeyBoundle *KeyBoundle::Build(yuki::SliceRef key,
                                         uint8_t type,
                                         uint64_t version_number,
                                         void *bytes,
                                         size_t bytes_size) {
    if (PredictBoundleSize(key, version_number) > bytes_size) {
        return nullptr; // too long
    }

    auto raw_buf = static_cast<uint8_t *>(bytes);
    raw_buf += yuki::Varint::Encode32(static_cast<uint32_t>(key.Length()),
                                      raw_buf);

    memcpy(raw_buf, key.Bytes(), key.Length());
    raw_buf += key.Length();

    raw_buf[0] = type;
    raw_buf++;

    raw_buf += yuki::Varint::Encode64(version_number, raw_buf);
    DCHECK_LE(raw_buf, static_cast<uint8_t *>(bytes) + bytes_size);
    return static_cast<KeyBoundle *>(bytes);
}

} // namespace yukino