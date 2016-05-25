#include <ndn-cxx/security/key-chain.hpp>

#include <boost/filesystem.hpp>
#include <fstream>

#include "lru_cache.h"

int main(int argc, char *argv[]) {
    entry::root_dir = "./cache";

    lru_cache cs = lru_cache(5, 5);
    ndn::KeyChain kc;

    auto start = boost::chrono::steady_clock::now();
    for(int i = 0; i<100; ++i) {
        auto data = std::make_shared<ndn::Data>(ndn::Name("/test/test" + std::to_string(i)));
        data->setFreshnessPeriod(boost::chrono::milliseconds(10000));
        kc.signWithSha256(*data);
        cs.insert(data);
    }
    std::cout << (boost::chrono::steady_clock::now() - start).count() << std::endl;

    return 0;
}