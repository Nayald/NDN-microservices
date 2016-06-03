#pragma once

#include <ndn-cxx/data.hpp>

#include <memory>

#include "entry.h"

class cs_cache {
protected:
    uint32_t nextPowerOf2(uint32_t n) {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        return ++n;
    }

public:
    virtual ~cs_cache() { };

    virtual void insert(ndn::Data data) = 0;

    virtual void insert(std::shared_ptr<ndn::Data> data_ptr) = 0;

    virtual std::shared_ptr<ndn::Data> tryGet(ndn::Name name) = 0;
};

