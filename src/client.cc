#include "client.h"
#include "worker.h"
#include "server.h"
#include "db.h"
#include "obj.h"
#include "key.h"
#include "configuration.h"
#include "value_traits.h"
#include "protocol.h"
#include "iterator.h"
#include "ae.h"
#include "yuki/varint.h"
#include "yuki/strings.h"
#include <stdarg.h>

namespace yukino {

const char *strnchr(const char *z, int ch, size_t n) {
    while(n--) {
        if (*z == ch) {
            return z;
        }
        ++z;
    }
    return nullptr;
}

Client::Client(Worker *worker, int fd, yuki::SliceRef ip, int port)
    : worker_(worker)
    , fd_(fd)
    , address_(ip.ToString())
    , port_(port) {
}

Client::~Client() {
    if (fd_ >= 0) {
        aeDeleteFileEvent(worker_->event_loop(), fd_, AE_WRITABLE|AE_WRITABLE);

        // TODO: to background thread
        close(fd_);
    }
}

yuki::Status Client::Init() {
    using yuki::Status;

    return Status::OK();
}

yuki::Status Client::IncomingRead() {
    using yuki::Status;
    using yuki::Slice;

    size_t readed = 0;
    while (readed < IO_BUF_SIZE) {
        size_t size;
        auto buf = input_buf_.OnceWriteBuffer(IO_BUF_SIZE - readed, &size);
        auto rv = read(fd_, buf, size);
        if (rv > 0) {
            input_buf_.Advance(rv);
            if (rv < size) {
                break;
            } else {
                readed += rv;
            }
        } else if (rv == 0) {
            return Status::Errorf(Status::kSystemError, "connection lost");
        } else {
            if (errno == EAGAIN) {
                break;
            } else {
                PLOG(ERROR) << "client read fail.";
                return Status::Errorf(Status::kSystemError, "io error");
            }
        }
    }

    std::string copied;
    yuki::Slice input;
    size_t proced = 0;

    switch (state_) {
        case STATE_INIT:
            // TXT\r\n
            // BIN\r\n
            if (input_buf_.read_remain() < 5) {
                break;
            }
            if (!input_buf_.CopiedReadIfNeed(5, &input, &copied)) {
                break;
            }

            if (input.Compare(yuki::Slice("TXT\r\n", 5)) == 0) {
                protocol_ = PROTO_TEXT;
                state_ = STATE_PROC;

                LOG(INFO) << "client " << address_ << ":" << port_
                          << " text protocol setup.";
            } else if (input.Compare(yuki::Slice("BIN\r\n", 5)) == 0) {
                protocol_ = PROTO_BIN;
                state_ = STATE_PROC;

                LOG(INFO) << "client " << address_ << ":" << port_
                          << " binary protocol setup.";
            } else {
                AddErrorReply("bad protocol setting. (TXT/BIN)");
                return Status::Errorf(Status::kCorruption,
                                      "bad protocol setting");
            }
            AddStringReply(Slice("ok", 2));
            break;

        case STATE_AUTH:
            // TODO:
            break;

        case STATE_PROC:
            if (input_buf_.read_remain() == 0) {
                break;
            }

            for (;;) {
                if (!input_buf_.CopiedReadIfNeed(IO_BUF_SIZE, &input, &copied)) {
                    break;
                }

                proced = 0;
                bool ok;
                if (protocol_ == PROTO_TEXT) {
                    ok = ProcessTextInputBuffer(input, &proced);
                } else {
                    ok = ProcessBinaryInputBuffer(input, &proced);
                }
                DCHECK_LE(proced, input.Length());
                input_buf_.Rewind(input.Length() - proced);
                if (!ok) {
                    break;
                }
            }
            break;

        default:
            break;
    }
    return Status::OK();
}

yuki::Status Client::OutgoingWrite() {
    using yuki::Status;

    ssize_t rv = write(fd_, &output_buf_[outbuf_written_],
                       outpos_ - outbuf_written_);
    if (rv < 0) {
        if (errno == EAGAIN) {
            return Status::OK();
        } else {
            PLOG(ERROR) << "client write fail.";
            return Status::Errorf(Status::kSystemError, "io error");
        }
    }

    outbuf_written_ += rv;
    if (outbuf_written_ == IO_BUF_SIZE || outbuf_written_ == outpos_) {
        outbuf_written_ = 0;
        outpos_ = 0;

        worker_->DeleteFileEvent(fd_, AE_WRITABLE);
    }
    return Status::OK();
}

bool Client::ProcessTextInputBuffer(yuki::SliceRef buf, size_t *proced) {
    using yuki::Slice;

    auto newline = strstr(buf.Data(), "\r\n");
    if (!newline || newline - buf.Data() > buf.Length()) {
        *proced = buf.Length();
        return false;
    }
    auto size = newline - buf.Data();

    std::vector<Handle<Obj>> args;
    Slice cmd;

    auto newarg = strnchr(buf.Data(), ' ', size);
    if (newarg) {
        cmd = Slice(buf.Data(), newarg - buf.Data());

         do {
             while (*newarg == ' ' && newarg < newline) {
                 newarg++;
             }
             if (newarg >= newline) {
                 break;
             }

             auto endarg = strnchr(newarg, ' ', newline - newarg);
             if (!endarg) {
                 args.emplace_back(String::New(Slice(newarg, newline - newarg)));
                 break;
             } else {
                 args.emplace_back(String::New(Slice(newarg, endarg - newarg)));
             }
             newarg = endarg;
         } while (newarg < newline);
    } else {
        cmd = Slice(buf.Data(), newline - buf.Data());
    }

    // SELECT 1
    // GET a
    // SET a b
    auto rv = ProcessCommand(cmd, Slice(), args);
    *proced = (newline - buf.Data()) + 2; // 2 == sizeof("\r\n")
    return rv;
}

bool Client::ProcessBinaryInputBuffer(yuki::SliceRef buf, size_t *proced) {
    // TODO
    return false;
}

#define APPEND_LOG() \
    do { \
        auto append_log_rv = db->AppendLog(cmd_entry->code, args); \
        if (append_log_rv.Failed()) { \
            AddErrorReply("%s append log fail: %s", cmd_entry->z, \
                          append_log_rv.ToString().c_str()); \
            return false; \
        } \
    } while (0)

#define GET_KEY(key, index) \
    String *key = nullptr; \
    do { \
        if (args[(index)]->type() != YKN_STRING) { \
            AddErrorReply("%s bad key type, expected STRING.", cmd_entry->z); \
            return false; \
        } \
        key = static_cast<String *>(args[(index)].get()); \
    } while (0)

bool Client::ProcessCommand(yuki::SliceRef cmd, yuki::SliceRef key,
                            const std::vector<Handle<Obj>> &args) {
    using yuki::Slice;
    using yuki::Status;

    auto db = worker_->server()->db(db_);
    auto cmd_entry = ::yukino_command(cmd.Data(),
                                      static_cast<unsigned>(cmd.Length()));
    if (!cmd_entry) {
        AddErrorReply("Command %.*s not support.", cmd.Length(), cmd.Data());
        return false;
    }

    if (cmd_entry->argc != 0) {
        if (cmd_entry->argc > 0) {
            if (args.size() < cmd_entry->argc) {
                AddErrorReply("%s bad arguments number, expect 1, actual %lu.",
                              cmd.ToString().c_str(), args.size());
                return false;
            }
        } else {
            // < 0
        }
    }

    switch (cmd_entry->code) {
    case CMD_SELECT: {
        int db = 0;
        if (args[0]->type() == YKN_STRING) {
            auto str = static_cast<String*>(args[0].get());

            if (!ValueTraits<int>::Parse(str->data(), &db)) {
                AddErrorReply("Bad type, not a integer.");
                return false;
            }
        } else if (args[0]->type() == YKN_INTEGER) {
            auto data = static_cast<Integer *>(args[0].get());
            db = static_cast<int>(data->data());
        } else {
            AddErrorReply("Bad type, expect integer.");
            return false;
        }

        const auto num_db = worker_->server()->conf().num_db_conf();
        if (db < 0 || db >= num_db) {
            AddErrorReply("SELECT db out of range. [%d, %d]", 0, num_db);
            return false;
        }
        db_ = db;
        AddStringReply(Slice("ok", 2));
    } return true;

    case CMD_GET: {
        String *key = static_cast<String *>(args[0].get());
        //auto rv = db->Put(key->data(), 0, args[1].get());
        Obj *value = nullptr;
        auto rv = db->Get(key->data(), nullptr, &value);
        if (rv.Failed()) {

            if (rv.Code() == Status::kNotFound) {
                AddObjReply(nullptr);
            } else {
                AddErrorReply("SET fail: %s", rv.ToString().c_str());
            }
            return false;
        }
        
        AddObjReply(value);
    } return true;

    case CMD_SET: {
        String *key = static_cast<String *>(args[0].get());

        APPEND_LOG();
        auto rv = db->Put(key->data(), 0, args[1].get());
        if (rv.Failed()) {
            AddErrorReply("SET fail: %s", rv.ToString().c_str());
            return false;
        }
        
        AddStringReply(Slice("ok", 2));
    } return true;

    case CMD_DELETE: {
        String *key = static_cast<String *>(args[0].get());

        APPEND_LOG();
        auto rv = db->Delete(key->data());
        if (rv) {
            AddIntegerReply(1);
        } else {
            AddIntegerReply(0);
        }
    } return true;

    case CMD_AUTH: {
        // TODO
    } return true;

    case CMD_KEYS: {
        int64_t limit = 0;
        if (args.size() > 0) {
            if (!ObjCastIntIf(args[0].get(), &limit)) {
                AddErrorReply("Bad type, expect integer.");
                return false;
            }
        }

        auto num_keys = db->num_keys();
        if (limit <= 0) {
            limit = num_keys;
        }

        AddArrayHead(limit < num_keys ? limit : num_keys);
        std::unique_ptr<Iterator> iter(db->iterator());
        for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
            if (limit-- <= 0) {
                break;
            }

            AddStringReply(iter->key()->key());
        }
    } return true;

    case CMD_LIST: {
        Handle<List> list(List::New());
        if (!list.get()) {
            AddErrorReply("LIST not enough memory");
            return false;
        }
        for (int i = 1; i < args.size(); i++) {
            list->stub()->InsertTail(args[i].get());
        }
        GET_KEY(key, 0);

        APPEND_LOG();
        auto rv = db->Put(key->data(), worker_->server()->current_milsces(),
                          list.get());
        if (rv.Failed()) {
            AddErrorReply("LIST can not be created, %s", rv.ToString().c_str());
            return false;
        }
        AddStringReply(Slice("ok", 2));
    } return true;

    case CMD_LPUSH:
    case CMD_RPUSH: {
        Handle<List> list;
        GET_KEY(key, 0);

        if (!GetList(key->data(), db, list.address())) {
            return false;
        }

        APPEND_LOG();
        for (int i = 1; i < args.size(); i++) {
            if (cmd_entry->code == CMD_LPUSH) {
                list->stub()->InsertHead(args[i].get());
            } else {
                list->stub()->InsertTail(args[i].get());
            }
        }
        AddIntegerReply(list->stub()->size());
    } return true;

    case CMD_LPOP:
    case CMD_RPOP: {
        Handle<List> list;
        GET_KEY(key, 0);

        if (!GetList(key->data(), db, list.address())) {
            return false;
        }

        APPEND_LOG();
        Obj *value = nullptr;
        if (cmd_entry->code == CMD_LPOP) {
            list->stub()->PopHead(&value);
        } else {
            // TODO:
        }
        AddObjReply(value);
    } return true;

    default:
        break;
    }

    AddErrorReply("Command %.*s not support.", cmd.Length(), cmd.Data());
    return false;
}

bool Client::GetList(yuki::SliceRef key, DB *db, List **list) {
    using yuki::Status;

    Obj *value;
    auto rv = db->Get(key, nullptr, &value);
    if (rv.Failed()) {
        if (rv.Code() == Status::kNotFound) {
            AddObjReply(nullptr);
        } else {
            AddErrorReply("LIST operation error, %s", rv.ToString().c_str());
        }
        return false;
    }
    if (value->type() == YKN_LIST) {
        AddErrorReply("Bad type, not a list");
        return false;
    }

    *list = static_cast<List *>(value);
    return true;
}

void Client::AddErrorReply(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string buf(yuki::Strings::Vformat(fmt, ap));
    va_end(ap);

    if (protocol_ == PROTO_TEXT) {
        if (outpos_ + 1 + buf.size() + 2 > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full, error: " << buf;
            return;
        }

        CreateEventIfNeed();
        output_buf_[outpos_++] = '-';
        memcpy(&output_buf_[outpos_], buf.data(), buf.size());
        outpos_ += buf.size();
        output_buf_[outpos_++] = '\r';
        output_buf_[outpos_++] = '\n';
    } else {
        // TODO: binary protocol

    }
}

void Client::AddStringReply(yuki::SliceRef buf) {
    if (protocol_ == PROTO_TEXT) {
        auto len = yuki::Strings::Format("%lu", buf.Length());
        // $<size>\r\n
        // <payload>\r\n
        if (outpos_ + 1 + len.size() + 2 + buf.Length() + 2 > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full, data: " << buf.ToString();
            return;
        }
        CreateEventIfNeed();

        output_buf_[outpos_++] = '$';
        memcpy(&output_buf_[outpos_], len.data(), len.size());
        outpos_ += len.size();
        output_buf_[outpos_++] = '\r';
        output_buf_[outpos_++] = '\n';
        memcpy(&output_buf_[outpos_], buf.Data(), buf.Length());
        outpos_ += buf.Length();
        output_buf_[outpos_++] = '\r';
        output_buf_[outpos_++] = '\n';
    } else {
        // [tag(1-byte)] [size(varint64)] [bytes]
        auto size = 1;
        size += yuki::Varint::Sizeof64(buf.Length());
        size += buf.Length();

        if (outpos_ + size > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full, data: " << buf.ToString();
            return;
        }

        CreateEventIfNeed();
        output_buf_[outpos_++] = TYPE_STRING;
        outpos_ += yuki::Varint::Encode64(buf.Length(),
                            reinterpret_cast<uint8_t *>(&output_buf_[outpos_]));
        memcpy(&output_buf_[outpos_], buf.Data(), buf.Length());
        outpos_ += buf.Length();
    }
}

void Client::AddIntegerReply(int64_t value) {
    if (protocol_ == PROTO_TEXT) {
        // max int64_t
        if (outpos_ + sizeof("-9223372036854775808") + 3 > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full!";
            return;
        }

        CreateEventIfNeed();
        snprintf(&output_buf_[outpos_], IO_BUF_SIZE, ":%" PRId64 "\r\n", value);
        outpos_ += strlen(&output_buf_[outpos_]);
    } else {
        auto size = yuki::Varint::Sizeof64(value);
        if (outpos_ + size > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full!";
            return;
        }

        CreateEventIfNeed();
        outpos_ += yuki::Varint::EncodeS64(value,
                            reinterpret_cast<uint8_t *>(&output_buf_[outpos_]));
    }
}

void Client::AddArrayHead(int64_t value) {
    if (protocol_ == PROTO_TEXT) {
        // *<array size>\r\n
        auto size = sizeof("-9223372036854775808") + 3;
        if (outpos_ + size > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full!";
            return;
        }

        CreateEventIfNeed();
        snprintf(&output_buf_[outpos_], IO_BUF_SIZE, "*%" PRId64 "\r\n", value);
        outpos_ += strlen(&output_buf_[outpos_]);
    } else {
        auto size = yuki::Varint::Sizeof64(value) + 1;
        if (outpos_ + size > IO_BUF_SIZE) {
            LOG(ERROR) << "output buffer full!";
            return;
        }

        CreateEventIfNeed();
        output_buf_[outpos_++] = TYPE_ARRAY;
        outpos_ += yuki::Varint::Encode64(value,
                            reinterpret_cast<uint8_t *>(&output_buf_[outpos_]));
    }
}

void Client::AddObjReply(Obj *ob) {
    using yuki::Slice;

    if (!ob) {
        if (protocol_ == PROTO_TEXT) {
            AddRawReply(Slice("$-1\r\n", 5));
        } else {
            AddRawReply(Slice("\0", 1));
        }
        return;
    }

    switch (ob->type()) {
    case YKN_STRING:
        AddStringReply(static_cast<String*>(ob)->data());
        break;

    case YKN_INTEGER:
        AddIntegerReply(static_cast<Integer*>(ob)->data());
        break;

    default:
        DLOG(FATAL) << "noreached";
        break;
    }
}

bool Client::AddRawReply(yuki::SliceRef buf) {
    if (outpos_ + buf.Length() > IO_BUF_SIZE) {
        LOG(ERROR) << "output buffer full!";
        return false;
    }

    CreateEventIfNeed();
    if (buf.Length() <= 16) {
        for (size_t i = 0; i < buf.Length(); i++) {
            output_buf_[outpos_++] = buf.Data()[i];
        }
    } else {
        memcpy(&output_buf_[outpos_], buf.Data(), buf.Length());
        outpos_ += buf.Length();
    }
    return true;
}

void Client::CreateEventIfNeed() {
    if (outpos_ == 0) {
        worker_->CreateFileEvent(fd_, AE_WRITABLE, this);
    }
}

} // namespace yukino