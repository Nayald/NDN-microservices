#include <ndn-cxx/security/key-chain.hpp>

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "tcp_server.h"

void signal(int n) {
    std::cerr << "close" << std::endl;
    exit(0);
}

int main(int argc, char *argv[]) {
    boost::asio::io_service ios;
    boost::asio::io_service::work work(ios);

    Tcp_server tcp(ios, "127.0.0.1", "6363");

    try {
        ios.run();
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}