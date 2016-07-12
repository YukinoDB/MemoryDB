#include "background.h"
#include "obj.h"
#include "glog/logging.h"
#include <fcntl.h>
#include <stdlib.h>

namespace yukino {

void BackgroundWorkQueue::PostEcho(yuki::SliceRef str) {
    BackgroundWork work;
    work.cmd = BackgroundWork::BKG_ECHO;
    auto tmp = static_cast<char *>(malloc(str.Length() + 1));
    memcpy(tmp, str.Data(), str.Length());
    tmp[str.Length()] = '\0';
    work.param.echo = tmp;
    Push(work);
}

Background::Background()
    : queue_(nullptr)
    , is_running_(0) {
}

Background::~Background() {
}

void Background::AsyncRun() {
    thread_ = std::move(std::thread([&] () {
        Work work;
        is_running_.store(true);

        pthread_setname_np("background");
        do {
            queue_->Take(&work);
            ProcessWork(&work);
        } while (work.cmd != Work::BKG_SHUTDOWN);

        is_running_.store(false);
    }));
}

void Background::ProcessWork(Work *work) {
    switch (work->cmd) {
        case Work::BKG_ECHO:
            printf("background echo: %s\n", work->param.echo);
            free(const_cast<char *>(work->param.echo));
            break;

        case Work::BKG_SHUTDOWN:
            // ignore it!
            break;

        case Work::BKG_SYNC_FILE:
            fsync(work->param.fd);
            //DLOG(INFO) << "fsync: " << work->param.fd;
            break;

        case Work::BKG_CLOSE_FILE:
            close(work->param.fd);
            //DLOG(INFO) << "close: " << work->param.fd;
            break;

        case Work::BKG_RELEASE_OBJECT:
            ObjRelease(work->param.obj);
            //DLOG(INFO) << "release: " << work->param.obj;
            break;

        default:
            break;
    }
}

    
} // namespace yukino