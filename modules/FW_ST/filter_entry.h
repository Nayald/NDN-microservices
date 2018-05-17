#pragma once

#include <ndn-cxx/data.hpp>

#include <memory>

class FilterEntry {
private:
    bool _drop;

public:
    explicit FilterEntry(bool drop);

    ~FilterEntry() = default;

    bool getDrop() const;

    std::string toJSON() const;
};