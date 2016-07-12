#ifndef YUKINO_BACKGROUND_H_
#define YUKINO_BACKGROUND_H_

#include <thread>
#include <mutex>
#include <list>
#include <atomic>
#include "yuki/slice.h"

namespace yukino {

struct Obj;

struct BackgroundWork {
    enum Code {
        BKG_CLOSE_FILE,
        BKG_SYNC_FILE,
        BKG_RELEASE_OBJECT,
        BKG_ECHO,
        BKG_SHUTDOWN,
    };

    Code cmd;

    union {
        Obj *obj;
        int   fd;
        const char *echo;
    } param;
}; // struct BackgroundWork

class BackgroundWorkQueue {
public:
    inline void PostCloseFile(int fd);
    inline void PostSyncFile(int fd);
    inline void PostReleaseObject(Obj *ob);
    void PostEcho(yuki::SliceRef str);
    inline void PostShutdown();

    inline void Push(const BackgroundWork &work);
    inline void Take(BackgroundWork *work);
private:
    std::mutex mutex_;
    std::condition_variable cond_;
    std::list<BackgroundWork> queue_;

}; // class BackgroundWorkQueue

class Background {
public:
    typedef BackgroundWorkQueue Delegate;
    typedef BackgroundWork      Work;

    Background();
    ~Background();

    void AsyncRun();
    void ProcessWork(Work *work);
    void WaitForShutdown() { thread_.join(); }

    Delegate *queue() const { return queue_; }
    void set_queue(Delegate *queue) { queue_ = queue; }

private:
    Delegate *queue_;
    std::thread thread_;
    std::atomic<bool> is_running_;
}; // class Background


inline void BackgroundWorkQueue::PostCloseFile(int fd) {
    BackgroundWork work;
    work.cmd = BackgroundWork::BKG_CLOSE_FILE;
    work.param.fd = fd;
    Push(work);
}

inline void BackgroundWorkQueue::PostSyncFile(int fd) {
    BackgroundWork work;
    work.cmd = BackgroundWork::BKG_SYNC_FILE;
    work.param.fd = fd;
    Push(work);
}

inline void BackgroundWorkQueue::PostReleaseObject(Obj *ob) {
    BackgroundWork work;
    work.cmd = BackgroundWork::BKG_RELEASE_OBJECT;
    work.param.obj = ob;
    Push(work);
}

inline void BackgroundWorkQueue::PostShutdown() {
    BackgroundWork work;
    work.cmd = BackgroundWork::BKG_SHUTDOWN;
    Push(work);
}

inline void BackgroundWorkQueue::Push(const BackgroundWork &work) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push_back(work);
    }
    cond_.notify_one();
}

inline void BackgroundWorkQueue::Take(BackgroundWork *work) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [&] () {return !queue_.empty(); });
    *work = queue_.front();
    queue_.pop_front();
}

} // namespace yukino

#endif // YUKINO_BACKGROUND_H_