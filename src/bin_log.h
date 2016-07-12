#ifndef YUKINO_BIN_LOG_H_
#define YUKINO_BIN_LOG_H_

#include "protocol.h"
#include "handle.h"
#include "yuki/slice.h"
#include "yuki/status.h"
#include <stdint.h>
#include <vector>
#include <memory>

namespace yukino {

struct Obj;
class OutputStream;
class InputStream;

class BinLogWriter {
public:
    BinLogWriter(OutputStream *stream, bool ownership, size_t block_size,
                 size_t initial_size);
    ~BinLogWriter();

    yuki::Status Append(CmdCode cmd_code, int64_t version,
                        const std::vector<Handle<Obj>> &args);

    void Reset(OutputStream *stream);

    size_t written_bytes() const { return written_bytes_; }
private:
    OutputStream *block_stream_;
    bool ownership_;
    size_t written_bytes_ = 0;
};

struct Operator {
    CmdCode cmd;
    int64_t version;
    std::vector<Handle<Obj>> args;
};

class BinLogReader {
public:
    BinLogReader(InputStream *stream, bool ownership, size_t block_size);
    ~BinLogReader();

    bool Read(Operator *op, yuki::Status *status);

private:
    InputStream *block_stream_;
    bool ownership_;
};

} // namespace yukino

#endif // YUKINO_BIN_LOG_H_