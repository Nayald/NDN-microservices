#include <ndn-cxx/security/key-chain.hpp>

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "lru_cache.h"
#include "tcp_server.h"

void signal(int n) {
    std::cerr << "close" << std::endl;
    exit(0);
}

int main(int argc, char *argv[]) {
    entry::root_dir = "./cache";

    lru_cache cs = lru_cache(100000, 00);

    /* auto start = boost::chrono::steady_clock::now();
     ndn::KeyChain kc;
     for (int i = 0; i < 200000; ++i) {
         auto data = std::make_shared<ndn::Data>(ndn::Name("debit/benchmark/" + std::to_string(i)));
         data->setFreshnessPeriod(boost::chrono::milliseconds(1000000));
         kc.signWithSha256(*data);
         cs.insert(data);
     }
     std::cerr << boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::steady_clock::now() - start).count() << "ms" << std::endl;*/

    boost::asio::io_service ios;
    boost::asio::io_service::work work(ios);

    tcp_server udp(ios, cs, "127.0.0.1", "6363");

    signal(SIGINT, signal);

    try {
        ios.run();
    } catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}