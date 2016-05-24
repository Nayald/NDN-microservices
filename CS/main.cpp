#include <ndn-cxx/security/key-chain.hpp>

#include <boost/filesystem.hpp>
#include <fstream>

#include "cs_cache.h"

int main(int argc, char *argv[]) {
    entry::root_dir="./cache";

    std::shared_ptr<ndn::Data> d = std::make_shared<ndn::Data>(ndn::Name("/test/test1/test2"));
    d->setFreshnessPeriod(boost::chrono::milliseconds(1000));
    ndn::KeyChain kc;
    kc.signWithSha256(*d);
    entry e(d);
    e.storeToDisk();
    std::cerr << e.toString() << std::endl;

    entry ee = entry::getFromDisk("/test/test1/test2");
    std::cerr << ee.toString() << std::endl;

    return 0;
}