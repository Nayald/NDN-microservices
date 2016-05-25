#pragma once

#include <map>
#include <list>

#include "cs_cache.h"

class lru_cache : public cs_cache {
private:
    uint32_t _ram_size, _disk_size;
    std::list<ndn::Name> _ram_registery, _disk_registery;

public:
    lru_cache(uint32_t ram_size, uint32_t disk_size);

    virtual ~lru_cache() override;

    virtual void insert(ndn::Data data) override;

    virtual void insert(std::shared_ptr<ndn::Data> data_ptr) override;

    virtual std::shared_ptr<ndn::Data> tryGet(ndn::Name name) override;
};