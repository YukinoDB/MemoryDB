#ifndef YUKINO_SERIALIZED_IO_H_
#define YUKINO_SERIALIZED_IO_H_

#include "basic_io.h"
#include "yuki/varint.h"
#include <stdint.h>

namespace yukino {

class SerializedOutputStream {
public:
    SerializedOutputStream(OutputStream *stream, bool ownership);
    ~SerializedOutputStream();

    inline size_t WriteByte(uint8_t byte);

    inline size_t WriteInt32(uint32_t value);
    inline size_t WriteSInt32(int32_t value);

    inline size_t WriteInt64(uint64_t value);
    inline size_t WriteSInt64(int64_t value);

    inline size_t WriteSlice(yuki::SliceRef value);
    inline size_t WriteSlice(const char *z, size_t n);
    inline size_t WriteString(const std::string &str);

    yuki::Status status() const { return stub_->status(); }

private:
    OutputStream *stub_;
    bool ownership_;
};

class SerializedInputStream {
public:
    SerializedInputStream(InputStream *stream, bool ownership);
    ~SerializedInputStream();

    inline bool ReadByte(uint8_t *value);

    bool ReadInt32(uint32_t *value);
    inline bool ReadSInt32(int32_t *value);

    bool ReadInt64(uint64_t *value);
    inline bool ReadSInt64(int64_t *value);

    inline size_t ReadString(yuki::Slice *value, std::string *stub);

    inline yuki::Status status() const;
    inline void set_status(const yuki::Status &s) { status_ = s; }
private:
    InputStream *stub_;
    bool ownership_;
    yuki::Status status_;
    std::string dummy_;
};

inline size_t SerializedOutputStream::WriteByte(uint8_t byte) {
    return stub_->Write(yuki::Slice(reinterpret_cast<char *>(&byte), 1));
}

inline size_t SerializedOutputStream::WriteInt32(uint32_t value) {
    char buf[yuki::Varint::kMax32Len];
    auto size = yuki::Varint::Encode32(value, buf);
    return stub_->Write(yuki::Slice(buf, size));
}

inline size_t SerializedOutputStream::WriteSInt32(int32_t value) {
    char buf[yuki::Varint::kMax32Len];
    auto size = yuki::Varint::EncodeS32(value, buf);
    return stub_->Write(yuki::Slice(buf, size));
}

inline size_t SerializedOutputStream::WriteInt64(uint64_t value) {
    char buf[yuki::Varint::kMax64Len];
    auto size = yuki::Varint::Encode64(value, buf);
    return stub_->Write(yuki::Slice(buf, size));
}

inline size_t SerializedOutputStream::WriteSInt64(int64_t value) {
    char buf[yuki::Varint::kMax64Len];
    auto size = yuki::Varint::EncodeS64(value, buf);
    return stub_->Write(yuki::Slice(buf, size));
}

inline size_t SerializedOutputStream::WriteSlice(yuki::SliceRef value) {
    auto size = WriteInt64(value.Length());
    return size + stub_->Write(value);
}

inline size_t SerializedOutputStream::WriteSlice(const char *z, size_t n) {
    return WriteSlice(yuki::Slice(z, n));
}

inline size_t SerializedOutputStream::WriteString(const std::string &str) {
    return WriteSlice(yuki::Slice(str));
}

inline bool SerializedInputStream::ReadByte(uint8_t *value) {
    yuki::Slice byte;
    auto ok = stub_->Read(1, &byte, &dummy_);
    if (ok && byte.Length() == 1) {
        *value = byte.Bytes()[0];
        return true;
    }
    return false;
}

inline bool SerializedInputStream::ReadSInt32(int32_t *value) {
    uint32_t stub;
    bool ok = ReadInt32(&stub);
    if (ok) {
        *value = yuki::ZigZag::Decode32(stub);
    }
    return ok;
}

inline bool SerializedInputStream::ReadSInt64(int64_t *value) {
    uint64_t stub;
    bool ok = ReadInt64(&stub);
    if (ok) {
        *value = yuki::ZigZag::Decode64(stub);
    }
    return ok;
}

inline size_t SerializedInputStream::ReadString(yuki::Slice *value,
                                                std::string *stub) {
    uint64_t len;
    auto ok = ReadInt64(&len);
    if (!ok) {
        return ok;
    }
    return stub_->Read(len, value, stub);
}

inline yuki::Status SerializedInputStream::status() const {
    if (status_.Ok()) {
        return stub_->status();
    }
    return status_;
}

} // namespace yukino

#endif // YUKINO_SERIALIZED_IO_H_