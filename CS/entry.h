#pragma once

#include <ndn-cxx/data.hpp>

#include <boost/chrono.hpp>

#include <memory>

class entry {
private:
    std::shared_ptr<ndn::Data> data_ptr;
    boost::chrono::steady_clock::time_point expire_time_point;

public:
    static std::string root_dir;

    entry(std::shared_ptr<ndn::Data> data);

    ~entry();

    bool isValid();

    long remaining();

    bool isInRam();

    std::shared_ptr<ndn::Data> getData();

    void storeToDisk();

    static void removeFromDisk(ndn::Name name);

    static std::shared_ptr<ndn::Data> getFromDisk(ndn::Name name);

    std::string toString() {
        return "Data Name: " + data_ptr->getName().toUri() + " have a size of " +
               std::to_string(data_ptr->wireEncode().value_size()) + " and will be valid for " +
               std::to_string(remaining()) + "ms (out-dated if negative)";
    }
};

