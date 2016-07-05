#ifndef YUKINO_SERVER_H_
#define YUKINO_SERVER_H_

#include <thread>
#include <memory>

struct aeEventLoop;

namespace yukino {

class Configuration;

//
// Server must run master thread
class Server {
public:
    Server();
    ~Server();

private:
    std::thread *workers_ = nullptr;
    int num_workers_ = 0;

    int listener_fd_ = -1;
    aeEventLoop *event_loop_ = nullptr;

    std::unique_ptr<Configuration> conf_;
}; // class Server

} // namespace yukino

#endif // YUKINO_SERVER_H_