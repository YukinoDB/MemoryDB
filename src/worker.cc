#include "worker.h"
#include "client.h"
#include "server.h"
#include "ae.h"
#include "anet.h"
#include <sys/socket.h>

namespace yukino {

Worker::Worker() {
}

Worker::~Worker() {
    if (event_loop_) {
        aeDeleteEventLoop(event_loop_);
    }
}

yuki::Status Worker::Init(Server *server, int id) {
    using yuki::Status;

    DCHECK(server_ == nullptr);
    server_ = DCHECK_NOTNULL(server);

    DCHECK_EQ(id_, 0);
    id_ = id;

    event_loop_ = aeCreateEventLoop(server_->num_events());
    if (!event_loop_) {
        return Status::Errorf(Status::kSystemError, "not enough memory");
    }
    return Status::OK();
}

yuki::Status Worker::PostIncomingFD(int fd, yuki::SliceRef ip, int host) {
    using yuki::Status;

    auto client = new Client(this, fd, ip, host);
    if (!client) {
        return Status::Errorf(Status::kSystemError, "not enough memory");
    }
    auto rv = client->Init();
    if (rv.Failed()) {
        delete client;
        return rv;
    }

    if (!CreateFileEvent(fd, AE_READABLE, client)) {
        delete client;
        return Status::Errorf(Status::kCorruption,
                              "create client event fail");
    }
    return Status::OK();
}

bool Worker::CreateFileEvent(int fd, int mask, Client *client) {
    std::unique_lock<std::mutex> lock(el_mutex_);

    return aeCreateFileEvent(event_loop_, fd, mask, HandleClientReadWrite,
                             client) == AE_OK;
}

void Worker::DeleteFileEvent(int fd, int mask) {
    std::unique_lock<std::mutex> lock(el_mutex_);

    aeDeleteFileEvent(event_loop_, fd, mask);
}

void Worker::AsyncRun() {
    thread_ = std::move(std::thread([] (aeEventLoop *el) {
        pthread_setname_np("worker");

        aeMain(DCHECK_NOTNULL(el));
    }, event_loop_));
}

void Worker::Stop() {
    {
        std::unique_lock<std::mutex> lock(el_mutex_);
        aeStop(event_loop_);
    }
    thread_.join();
}

/* static */
void Worker::HandleClientReadWrite(aeEventLoop *, int fd, void *data, int mask) {
    using yuki::Status;

    auto client = static_cast<Client *>(DCHECK_NOTNULL(data));
    int sockerr = 0;
    socklen_t errlen = sizeof(sockerr);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &errlen) == -1)
        sockerr = errno;
    if (sockerr) {
        PLOG(ERROR) << "client networking error";
        delete client;
        return;
    }

    Status rv;
    if (mask & AE_READABLE) {
        rv = client->IncomingRead();
        if (rv.Failed()) {
            LOG(ERROR) << "client " << client->address() << ":"
                       << client->port() << " read fail";

            delete client;
            return;
        }
    }

    if (mask & AE_WRITABLE) {
        rv = client->OutgoingWrite();
        if (rv.Failed()) {
            LOG(ERROR) << "client " << client->address() << ":"
                       << client->port() << " write fail";

            delete client;
            return;
        }
    }
}

} // namespace yukino