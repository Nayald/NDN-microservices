#include <ndn-cxx/security/key-chain.hpp>

#include "cs_cache.h"

int main(int argc, char *argv[]) {
    entry::root_dir="./cache";
    std::shared_ptr<ndn::Data> d = std::make_shared<ndn::Data>(ndn::Name("/test/test1/test2"));
    d->setFreshnessPeriod(boost::chrono::milliseconds(0));
    ndn::KeyChain kc;
    kc.signWithSha256(*d);
    entry *e = new entry(d);
    e->storeToDisk();
    std::cout << d << std::endl;
    return 0;
}