#include "client.h"
#include "worker.h"
#include "server.h"
#include "obj.h"
#include "key.h"
#include "value_traits.h"
#include "ae.h"
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

bool Client::ProcessCommand(yuki::SliceRef cmd, yuki::SliceRef key,
                            const std::vector<Handle<Obj>> &args) {
    using yuki::Slice;

    if (cmd.Compare(Slice("SELECT", 6)) == 0) {
        if (args.size() < 1) {
            AddErrorReply("SELECT bad arguments number, expect 1, actual %z.",
                          args.size());
            return false;
        }

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

        db_ = db; // TODO:
        AddStringReply(Slice("ok", 2));
        return true;
    }

    AddErrorReply("Command %.*s not support.", cmd.Length(), cmd.Data());
    return false;
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

// ERROR
// STRING
// INTEGER
// ARRAY
//
// ARRAY 2
//   ARRAY 2
//     STRING  name
//     STRING  jake
//   ARRAY 2
//     STRING  id
//     INTEGER 100
//
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
        // TODO: binary protocol

        CreateEventIfNeed();
    }
}

void Client::CreateEventIfNeed() {
    if (outpos_ == 0) {
        worker_->CreateFileEvent(fd_, AE_WRITABLE, this);
    }
}

} // namespace yukino