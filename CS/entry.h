#pragma once

#include <ndn-cxx/data.hpp>

#include <boost/chrono.hpp>

#include <memory>

class entry{
private:
    std::shared_ptr<ndn::Data> data_ptr;
    boost::chrono::steady_clock::time_point expire_time_point;

public:
    static std::string root_dir;

    entry(std::shared_ptr<ndn::Data> data);

    ~entry();

    bool isValid();

    bool isInRam();

    void storeToDisk();

    std::shared_ptr<ndn::Data> getData();

    std::shared_ptr<ndn::Data> getData(ndn::Name name);
};

