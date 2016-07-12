#include "serialized_io.h"
#include "crc32.h"

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

VerifiedOutputStreamProxy::VerifiedOutputStreamProxy(OutputStream *stream,
                                                     bool ownership,
                                                     uint32_t initial)
    : stub_(DCHECK_NOTNULL(stream))
    , ownership_(ownership)
    , crc32_checksum_(initial) {
}

VerifiedOutputStreamProxy::~VerifiedOutputStreamProxy() {
    if (ownership_) {
        delete stub_;
    }
}

size_t VerifiedOutputStreamProxy::Write(const void *buf, size_t size) {
    crc32_checksum_ = crc32(crc32_checksum_, buf, size);
    return stub_->Write(buf, size);
}

yuki::Status VerifiedOutputStreamProxy::status() const {
    return stub_->status();
}

VerifiedInputStreamProxy::VerifiedInputStreamProxy(InputStream *stream,
                                                   bool ownership,
                                                   uint32_t initial)
    : stub_(DCHECK_NOTNULL(stream))
    , ownership_(ownership)
    , crc32_checksum_(initial) {
}

VerifiedInputStreamProxy::~VerifiedInputStreamProxy() {
    if (ownership_) {
        delete stub_;
    }
}

bool VerifiedInputStreamProxy::ReadLine(std::string *line) {
    auto ok = stub_->ReadLine(line);
    if (ok) {
        crc32_checksum_ = crc32(crc32_checksum_, line->data(), line->size());
    }
    return ok;
}

bool VerifiedInputStreamProxy::Read(size_t size, yuki::Slice *buf, std::string *stub) {
    auto ok = stub_->Read(size, buf, stub);
    if (ok) {
        crc32_checksum_ = crc32(crc32_checksum_, buf->Data(), buf->Length());
    }
    return ok;
}

yuki::Status VerifiedInputStreamProxy::status() const {
    return stub_->status();
}

} // namespace yukion