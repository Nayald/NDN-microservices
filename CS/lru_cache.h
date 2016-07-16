#pragma once

#include <unordered_map>

#include "cs_cache.h"

class lru_cache : public cs_cache {
private:
    std::unordered_map<ndn::Name, std::list<entry>::iterator> _entries;
    uint32_t _ram_current_size, _disk_current_size, _ram_max_size, _disk_max_size;
    std::list<entry> _ram_registery, _disk_registery;

public:
    lru_cache(uint32_t ram_size, uint32_t disk_size);

    virtual ~lru_cache() override;

    virtual void insert(ndn::Data &data) override;

    virtual void insert(std::shared_ptr<ndn::Data> data_ptr) override;

    virtual std::shared_ptr<ndn::Data> tryGet(ndn::Name name) override;

    virtual void checkSize();

    std::string toString(){
        std::string s = "ram:\n";
        for(auto it = _ram_registery.begin(); it != _ram_registery.end(); ++it){
            s += it->toString();
            s += "\n";
        }
        s += "\ndisk:\n";
        for(auto it = _disk_registery.begin(); it != _disk_registery.end(); ++it){
            s += it->toString();
            s += "\n";
        }
        s += "\n";
        return s;
    }
};