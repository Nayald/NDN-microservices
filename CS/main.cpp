#include <ndn-cxx/security/key-chain.hpp>

#include <boost/filesystem.hpp>
#include <fstream>

#include "lru_cache.h"

int main(int argc, char *argv[]) {
    entry::root_dir = "./cache";

    lru_cache cs = lru_cache(5, 5);
    ndn::KeyChain kc;

    auto data1 = std::make_shared<ndn::Data>(ndn::Name("/test/test1"));
    kc.signWithSha256(*data1);
    auto data2 = std::make_shared<ndn::Data>(ndn::Name("/test/test2"));
    kc.signWithSha256(*data2);
    auto data3 = std::make_shared<ndn::Data>(ndn::Name("/test/test3"));
    kc.signWithSha256(*data3);
    auto data4 = std::make_shared<ndn::Data>(ndn::Name("/test/test4"));
    kc.signWithSha256(*data4);
    auto data5 = std::make_shared<ndn::Data>(ndn::Name("/test/test5"));
    kc.signWithSha256(*data5);
    auto data6 = std::make_shared<ndn::Data>(ndn::Name("/test/test6"));
    kc.signWithSha256(*data6);
    auto data7 = std::make_shared<ndn::Data>(ndn::Name("/test/test7"));
    kc.signWithSha256(*data7);
    auto data8 = std::make_shared<ndn::Data>(ndn::Name("/test/test8"));
    kc.signWithSha256(*data8);
    auto data9 = std::make_shared<ndn::Data>(ndn::Name("/test/test9"));
    kc.signWithSha256(*data9);
    auto data10 = std::make_shared<ndn::Data>(ndn::Name("/test/test10"));
    kc.signWithSha256(*data10);
    auto data11 = std::make_shared<ndn::Data>(ndn::Name("/test/test11"));
    kc.signWithSha256(*data11);

    cs.insert(data1);
    cs.insert(data2);
    cs.insert(data3);
    cs.insert(data4);
    cs.insert(data5);
    cs.insert(data6);
    cs.insert(data7);
    cs.insert(data8);
    cs.insert(data9);
    cs.insert(data10);
    cs.insert(data11);

    auto data5b = cs.tryGet("/test/test5");
    std::cout << *data5b << std::endl;
    auto data1b = cs.tryGet("/test/test1");
    std::cout << *data1b << std::endl;
    auto data1bb = cs.tryGet("/test/test1");
    std::cout << *data1bb << std::endl;
    std::cout << (*data1bb==*data1bb) << std::endl;

    return 0;
}