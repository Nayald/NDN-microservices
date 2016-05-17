#include "lru_cache.h"

lru_cache::lru_cache(uint32_t ram_size, uint32_t disk_size) {
    _ram_size=ram_size;
    _disk_size=disk_size;
}

lru_cache::~lru_cache() {

}

void lru_cache::insert(ndn::Data data) {}

void lru_cache::insert(std::shared_ptr<ndn::Data> data_ptr) {}

std::shared_ptr<ndn::Data> lru_cache::tryGet(ndn::Name name) {
    return std::shared_ptr<ndn::Data>();
}