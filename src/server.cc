#include "server.h"
#include "worker.h"
#include "db.h"
#include "configuration.h"
#include "ae.h"
#include "anet.h"
#include <sys/time.h>

namespace yukino {

Server::Server(const std::string &conf_file, Configuration *conf, int num_events)
    : num_events_(num_events)
    , conf_file_(conf_file)
    , conf_(conf) {
    DCHECK_LT(0, num_events_);
}

Server::~Server() {
    if (event_loop_ && listener_fd_ >= 0) {
        aeDeleteFileEvent(event_loop_, listener_fd_, AE_READABLE);
        close(listener_fd_);
    }

    aeDeleteEventLoop(event_loop_);
    delete[] workers_;

    for (int i = 0; i < conf().num_db_conf(); i++) {
        delete dbs_[i];
    }
    delete[] dbs_;
}

yuki::Status Server::Init() {
    using yuki::Status;

    if (conf().num_db_conf() > 0) {
        dbs_ = new DB *[conf().num_db_conf()];
        if (!dbs_) {
            return Status::Errorf(Status::kSystemError, "not enough memory");
        }

        memset(dbs_, 0, sizeof(DB *) * conf().num_db_conf());
        for (size_t i = 0; i < conf().num_db_conf(); i++) {
            dbs_[i] = DB::New(conf().db_conf(i), conf().data_dir(),
                              static_cast<int>(i));
            if (!dbs_[i]) {
                return Status::Errorf(Status::kSystemError,
                                      "not enough memory");
            }
            auto rv = dbs_[i]->Open();
            if (rv.Failed()) {
                return rv;
            }
        }
    }

    DCHECK(event_loop_ == nullptr);
    event_loop_ = aeCreateEventLoop(num_events_);
    if (!event_loop_) {
        return Status::Errorf(Status::kSystemError, "not enough memory");
    }

    auto addr = conf().address();
    listener_fd_ = anetTcpServer(nullptr, conf().port(), &addr[0], 1024);
    if (listener_fd_ < 0) {
        return Status::Errorf(Status::kSystemError, "listen %s:%d fail",
                              addr.c_str(), conf().port());
    }
    aeCreateFileEvent(event_loop_, listener_fd_, AE_READABLE,
                      HandleListenAccept, this);

    int num_workers = conf().num_workers();
    workers_ = new Worker[num_workers];
    if (!workers_) {
        return Status::Errorf(Status::kSystemError, "not enough memory");
    }

    for (int i = 0; i < num_workers; i++) {
        auto rv = workers_[i].Init(this, i);
        if (rv.Failed()) {
            return rv;
        }
    }
    return Status::OK();
}

void Server::Run() {
    int num_workers = conf().num_workers();
    for (int i = 0; i < num_workers; i++) {
        workers_[i].AsyncRun();
    }

    aeMain(event_loop_);
}

DB *Server::db(int i) {
    DCHECK_GE(i, 0);
    DCHECK_LT(i, conf().num_db_conf());

    return DCHECK_NOTNULL(dbs_[i]);
}

int64_t Server::current_milsces() const {
    struct timeval tv;
    if (::gettimeofday(&tv, nullptr) != 0) {
        PLOG(ERROR) << "can not get time!";
        return 0;
    }

    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

/*static*/
void Server::HandleListenAccept(aeEventLoop *, int listener, void *data, int) {
    auto self = static_cast<Server *>(data);

    char ip[128];
    int port = 0;
    int fd = anetTcpAccept(nullptr, listener, ip, arraysize(ip), &port);
    if (fd < 0) {
        PLOG(ERROR) << "accept fail!";
    } else {
        DLOG(INFO) << "accept " << ip << ":" << port;
        DCHECK_NOTNULL(self)->IncomingClientAccept(fd, yuki::Slice(ip), port);
    }
}

void Server::IncomingClientAccept(int client_fd, yuki::SliceRef ip, int port) {
    int i = rand() % conf().num_workers();

    DCHECK(client_fd >= 0);
    workers_[i].PostIncomingFD(client_fd, ip, port);
}

} // namespace yukino