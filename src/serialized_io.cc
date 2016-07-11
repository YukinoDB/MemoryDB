#include "serialized_io.h"

namespace yukino {

SerializedOutputStream::SerializedOutputStream(OutputStream *stream, bool ownership)
    : stub_(stream)
    , ownership_(ownership) {
}

SerializedOutputStream::~SerializedOutputStream() {
    if (ownership_) {
        delete stub_;
    }
}

SerializedInputStream::SerializedInputStream(InputStream *stream, bool ownership)
    : stub_(stream)
    , ownership_(ownership) {
}

SerializedInputStream::~SerializedInputStream() {
    if (ownership_) {
        delete stub_;
    }
}

bool SerializedInputStream::ReadInt32(uint32_t *value) {
    using yuki::Status;

    *value = 0;
    uint8_t byte = 0;
    int i;
    for (i = 0; i < yuki::Varint::kMax32Len; i++) {
        bool ok = ReadByte(&byte);
        if (!ok) {
            return false;
        }
        if (byte >= 0x80) {
            *value |= (byte & 0x7f);
            *value <<= 7;
        } else {
            *value |= (byte & 0x7f);
            break;
        }
    }
    if (i == yuki::Varint::kMax32Len && (byte & 0x80)) {
        status_ = Status::Corruptionf("varint32 too large");
    }
    return true;
}

bool SerializedInputStream::ReadInt64(uint64_t *value) {
    using yuki::Status;

    *value = 0;
    uint8_t byte = 0;
    int i;
    for (i = 0; i < yuki::Varint::kMax64Len; i++) {
        bool ok = ReadByte(&byte);
        if (!ok) {
            return false;
        }
        if (byte >= 0x80) {
            *value |= (byte & 0x7f);
            *value <<= 7;
        } else {
            *value |= (byte & 0x7f);
            break;
        }
    }
    if (i == yuki::Varint::kMax64Len && (byte & 0x80)) {
        status_ = Status::Corruptionf("varint64 too large");
    }
    return true;
}

} // namespace yukion