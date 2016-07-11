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
    BinLogWriter(OutputStream *stream, bool ownership, size_t block_size);
    ~BinLogWriter();

    yuki::Status Append(CmdCode cmd_code,
                        const std::vector<Handle<Obj>> &args);

private:
    OutputStream *block_stream_;
    bool ownership_;
};

class BinLogReader {
public:
    BinLogReader(InputStream *stream, bool ownership, size_t block_size);
    ~BinLogReader();

    bool Read(CmdCode *cmd_code, std::vector<Handle<Obj>> *args,
              yuki::Status *status);

private:
    InputStream *block_stream_;
    bool ownership_;
};

//struct Operator {
//    int32_t cmd_code;
//    Obj   **args;
//    int     argc;
//};

} // namespace yukino

#endif // YUKINO_BIN_LOG_H_