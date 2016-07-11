#include "bin_log.h"
#include "basic_io.h"
#include "serialized_io.h"
#include "obj.h"

namespace yukino {

BinLogWriter::BinLogWriter(OutputStream *stream, bool ownership, size_t block_size)
    : block_stream_(stream)
    , ownership_(ownership) {
}

BinLogWriter::~BinLogWriter() {
    if (ownership_) {
        delete block_stream_;
    }
}

// [code(byte)] [version(varint64)] [argc(varint32)] [payload]
yuki::Status BinLogWriter::Append(CmdCode cmd_code, int64_t version,
                                  const std::vector<Handle<Obj>> &args) {
    using yuki::Status;
    using yuki::Slice;

    SerializedOutputStream serializer(block_stream_, false);

    written_bytes_ += serializer.WriteByte(static_cast<char>(cmd_code));
    written_bytes_ += serializer.WriteSInt64(version);
    written_bytes_ += serializer.WriteInt32(static_cast<uint32_t>(args.size()));
    for (const auto &obj : args) {
        written_bytes_ += ObjSerialize(obj.get(), &serializer);
    }
    return block_stream_->status();
}

void BinLogWriter::Reset(OutputStream *stream) {
    if (stream == block_stream_) {
        return;
    }
    if (ownership_) {
        delete block_stream_;
    }
    block_stream_ = stream;
    written_bytes_ = 0;
}

BinLogReader::BinLogReader(InputStream *stream, bool ownership,
                           size_t block_size)
    : block_stream_(stream)
    , ownership_(ownership) {
}

BinLogReader::~BinLogReader() {
    if (ownership_) {
        delete block_stream_;
    }
}

#define CALL(expr) if (!expr) { return false; } (void)0

bool BinLogReader::Read(Operator *op, yuki::Status *status) {
    using yuki::Status;
    using yuki::Slice;

    SerializedInputStream deserializer(block_stream_, false);
    uint8_t byte;
    CALL(deserializer.ReadByte(&byte));
    op->cmd = static_cast<CmdCode>(byte);

    CALL(deserializer.ReadSInt64(&op->version));

    op->args.clear();
    uint32_t n;
    CALL(deserializer.ReadInt32(&n));
    while (n--) {
        auto obj = ObjDeserialize(&deserializer);
        if (!obj) {
            *status = block_stream_->status();
            return false;
        }

        op->args.emplace_back(obj);
    }
    return true;
}

} // namespace yukino