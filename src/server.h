#ifndef YUKINO_SERVER_H_
#define YUKINO_SERVER_H_

#include "yuki/file_path.h"
#include "yuki/slice.h"
#include <thread>
#include <string>
#include <memory>

struct aeEventLoop;

namespace yukino {

class Worker;
class Configuration;

//
// Server must run master thread
class Server {
public:
    Server(const std::string &conf_file, Configuration *conf, int num_events);
    Server(const Server &) = delete;
    Server(Server &&) = delete;
    void operator = (const Server &) = delete;

    ~Server();

    yuki::Status Init();
    void Run();

    aeEventLoop *event_loop() { return event_loop_; }

    const Configuration &conf() const { return *conf_; }
    Configuration *mutable_conf() { return conf_.get(); }

    int num_events() const { return num_events_; }

private:
    static void HandleListenAccept(aeEventLoop *el, int fd, void *data,
                                   int mask);

    void IncomingClientAccept(int client_fd, yuki::SliceRef ip, int port);

    int listener_fd_ = -1;
    aeEventLoop *event_loop_ = nullptr;

    const int num_events_ = 0;
    yuki::FilePath conf_file_;
    std::unique_ptr<Configuration> conf_;

    Worker *workers_;
}; // class Server

} // namespace yukino

#endif // YUKINO_SERVER_H_