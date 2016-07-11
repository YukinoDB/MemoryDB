#ifndef YUKINO_CLIENT_H_
#define YUKINO_CLIENT_H_

#include "circular_buffer.h"
#include "handle.h"
#include "yuki/status.h"
#include "yuki/slice.h"
#include <string>
#include <vector>

struct command;

namespace yukino {

struct Obj;
class Worker;
class Server;
class DB;
class List;
typedef struct command Command;

class Client {
public:
    enum {
        IO_BUF_SIZE = 5 * 1024,
    };

    enum State {
        STATE_INIT,
        STATE_AUTH,
        STATE_PROC,
    };

    enum Protocol {
        PROTO_TEXT,
        PROTO_BIN,
    };

    //typedef StaticCircularBuffer<IO_BUF_SIZE> CircularBuffer;

    Client(Worker *worker, int fd, yuki::SliceRef ip, int port);
    Client(const Client &) = delete;
    Client(Client &&) = delete;
    void operator = (const Client &) = delete;

    ~Client();

    yuki::Status Init();

    yuki::Status IncomingRead();
    yuki::Status OutgoingWrite();

    bool ProcessTextInputBuffer(yuki::SliceRef buf, size_t *proced);
    bool ProcessBinaryInputBuffer(yuki::SliceRef buf, size_t *proced);

    bool ProcessCommand(const Command &cmd,
                        yuki::SliceRef key,
                        const std::vector<Handle<Obj>> &args);

    bool GetList(yuki::SliceRef key, DB *db, List **list);

    void AddErrorReply(const char *fmt, ...);
    void AddStringReply(yuki::SliceRef str);
    void AddIntegerReply(int64_t value);
    void AddObjReply(Obj *ob);
    void AddArrayHead(int64_t size);
    bool AddRawReply(yuki::SliceRef buf);

    void CreateEventIfNeed();

    int port() const { return port_; }
    const std::string &address() const { return address_; }

private:
    State state_ = STATE_INIT;
    Worker *worker_ = nullptr;
    int fd_ = -1;
    std::string address_;
    int port_ = -1;
    Protocol protocol_ = PROTO_TEXT;

    StaticCircularBuffer<IO_BUF_SIZE> input_buf_;
    char output_buf_[IO_BUF_SIZE];
    size_t outpos_ = 0;
    size_t outbuf_written_ = 0;

    int db_ = 0;
};

} // namespace yukino

#endif // YUKINO_CLIENT_H_