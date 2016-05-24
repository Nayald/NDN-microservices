#include "lru_cache.h"

lru_cache::lru_cache(uint32_t ram_size, uint32_t disk_size) {
    _ram_size = nextPowerOf2(ram_size);
    _disk_size = nextPowerOf2(disk_size);
}

lru_cache::~lru_cache() {

}

void lru_cache::insert(ndn::Data data) {
    ndn::Name name = data.getName();
    _entries[name] = entry(data.shared_from_this());
    _ram_registery.emplace_front(name);
    if (_ram_registery.size() > _ram_size) {
        name = _ram_registery.back();
        auto it = _entries.find(name);
        it->second.storeToDisk();
        it->second = entry();
        _disk_registery.emplace_front(name);
        _ram_registery.pop_back();
    }
    if (_disk_registery.size() > _disk_size) {
        name = _disk_registery.back();
        _entries.erase(name);
        entry::removeFromDisk(name);
        _ram_registery.pop_back();
    }
}

void lru_cache::insert(std::shared_ptr<ndn::Data> data_ptr) {
    ndn::Name name = data_ptr->getName();
    _entries[name] = entry(data_ptr);
    _ram_registery.emplace_front(name);
    if (_ram_registery.size() > _ram_size) {
        name = _ram_registery.back();
        auto it = _entries.find(name);
        it->second.storeToDisk();
        it->second = entry();
        _disk_registery.emplace_front(_ram_registery.front());
        _ram_registery.pop_back();
    }
    if (_disk_registery.size() > _disk_size) {
        name = _disk_registery.back();
        _entries.erase(name);
        entry::removeFromDisk(name);
        _ram_registery.pop_back();
    }
}

std::shared_ptr<ndn::Data> lru_cache::tryGet(ndn::Name name) {
    auto it = _entries.find(name);
    if (it != _entries.end()) {
        entry e = it->second;
        if (e.getData()) {
            auto it = _ram_registery.begin(), it2 = it;
            while (*it2 != name) {
                ++it2;
            }
            std::swap(*it, *it2);
            return e.getData();
        }
        e = entry::getFromDisk(name);
        if (e.getData()) {
            insert(e.getData());
            return e.getData();
        }
    }
    return nullptr;
}