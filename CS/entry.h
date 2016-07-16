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

    entry();

    entry(ndn::Data &data);

    entry(std::shared_ptr<ndn::Data> data);

    ~entry();

    bool isValid() const;

    long remaining() const;

    void validFor(long milliseconds);

    std::shared_ptr<ndn::Data> getData() const;

    void storeToDisk() const;

    static void removeFromDisk(ndn::Name name);

    static entry getFromDisk(ndn::Name name);

    std::string toString() {
        return "Data Name: " + data_ptr->getName().toUri() + " have a size of " +
               std::to_string(data_ptr->wireEncode().value_size()) + " and is still valid for " +
               std::to_string(remaining()) + "ms";
    }
};

