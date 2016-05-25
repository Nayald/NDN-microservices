#include "lru_cache.h"
#include "udp_server.h"

int main(int argc, char *argv[]) {
    entry::root_dir = "./cache";

    lru_cache cs = lru_cache(5, 5);


    boost::asio::io_service ios;
    boost::asio::io_service::work work(ios);

    udp_server udp(ios);

    ios.run();

    return 0;
}