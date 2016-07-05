#include "server.h"
#include "configuration.h"

namespace yukino {

Server::Server()
    : conf_(new Configuration()) {
}

Server::~Server() {
}

} // namespace yukino