#ifndef YUKINO_WORKER_H_
#define YUKINO_WORKER_H_

#include "yuki/status.h"
#include "yuki/slice.h"
#include <thread>
#include <mutex>

typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData,
                        int mask);
struct aeEventLoop;

namespace yukino {

class Server;
class Client;

class Worker {
public:
    Worker();
    Worker(const Worker &) = delete;
    Worker(Worker &&) = delete;
    void operator = (const Worker &) = delete;

    ~Worker();

    yuki::Status Init(Server *server, int id);

    yuki::Status PostIncomingFD(int fd, yuki::SliceRef ip, int port);

    bool CreateFileEvent(int fd, int mask, Client *client);
    void DeleteFileEvent(int fd, int mask);

    void AsyncRun();

    void Stop();

    int id() const { return id_; }
    Server *server() const { return server_; }
    aeEventLoop *event_loop() const { return event_loop_; }

private:
    static void HandleClientReadWrite(aeEventLoop *el, int fd, void *data,
                                      int mask);

    int id_ = 0;
    Server *server_ = nullptr;
    aeEventLoop *event_loop_ = nullptr;

    std::thread thread_;
    std::mutex el_mutex_;
}; // class Worker

} // namespace yukino

#endif // YUKINO_WORKER_H_