#pragma once

#include <ndn-cxx/data.hpp>

#include <memory>

class CacheEntry {
private:
    const ndn::Data _data;
    const ndn::time::steady_clock::time_point expire_time_point;

public:
    explicit CacheEntry(const ndn::Data &data);

    ~CacheEntry() = default;

    const ndn::Data getData() const;

    bool isValid() const;

    ndn::time::milliseconds remainingTime() const;
};