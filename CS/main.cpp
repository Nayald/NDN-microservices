#include <ndn-cxx/security/key-chain.hpp>

#include <boost/asio.hpp>

#include "lru_cache.h"
#include "udp_server.h"

void signal(int n) {
    std::cerr << "close" << std::endl;
    exit(0);
}

int main(int argc, char *argv[]) {
    entry::root_dir = "./cache";

    lru_cache cs = lru_cache(10000, 0);

    /*ndn::KeyChain kc;
    for (int i = 0; i < 500; ++i) {
        auto data = std::make_shared<ndn::Data>(ndn::Name("debit/benchmark/" + std::to_string(i)));
        data->setFreshnessPeriod(boost::chrono::milliseconds(1000000));
        kc.signWithSha256(*data);
        //std::cout << data->getName() << std::endl;
        cs.insert(data);
    }*/
    std::cout << "ok" << std::endl;

    boost::asio::io_service ios;
    boost::asio::io_service::work work(ios);

    udp_server udp(ios, cs, "127.0.0.1", "6363");

    signal(SIGINT, signal);

    try {
        ios.run();
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}