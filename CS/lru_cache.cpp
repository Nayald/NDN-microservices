#include "lru_cache.h"

lru_cache::lru_cache(uint32_t ram_size, uint32_t disk_size) {
    _ram_size = nextPowerOf2(ram_size);
    _disk_size = nextPowerOf2(disk_size);
}

lru_cache::~lru_cache() {

}

void lru_cache::insert(ndn::Data data) {
    if (data.getFreshnessPeriod().count() > 0) {
        ndn::Name name = data.getName();
        _entries[name] = entry(data.shared_from_this());
        _ram_registery.emplace_front(name);
        if (_ram_registery.size() > _ram_size) {
            ndn::Name ram_name = _ram_registery.back();
            if (_disk_size > 0) {
                auto it = _entries.find(ram_name);
                it->second.storeToDisk();
                it->second = entry();
                _disk_registery.splice(_disk_registery.begin(), _ram_registery, (++_ram_registery.rbegin()).base());
            } else {
                _ram_registery.pop_back();
                return;
            }
        }
        if (_disk_registery.size() > _disk_size) {
            ndn::Name disk_name = _disk_registery.back();
            _entries.erase(disk_name);
            entry::removeFromDisk(disk_name);
            _disk_registery.pop_back();
        }
    }
    std::cout << "ram: " << std::endl;
    for (ndn::Name e : _ram_registery) {
        std::cout << e << std::endl;
    }
    std::cout << std::endl;
    std::cout << "disk: " << std::endl;
    for (ndn::Name e : _disk_registery) {
        std::cout << e << std::endl;
    }
}

void lru_cache::insert(std::shared_ptr<ndn::Data> data_ptr) {
    if (data_ptr->getFreshnessPeriod().count() > 0) {
        ndn::Name name = data_ptr->getName();
        _entries[name] = entry(data_ptr);
        _ram_registery.emplace_front(name);
        if (_ram_registery.size() > _ram_size) {
            ndn::Name ram_name = _ram_registery.back();
            if (_disk_size > 0) {
                auto it = _entries.find(ram_name);
                it->second.storeToDisk();
                it->second = entry();
                _disk_registery.splice(_disk_registery.begin(), _ram_registery, (++_ram_registery.rbegin()).base());
            } else {
                _ram_registery.pop_back();
                return;
            }
        }
        if (_disk_registery.size() > _disk_size) {
            ndn::Name disk_name = _disk_registery.back();
            _entries.erase(disk_name);
            entry::removeFromDisk(disk_name);
            _disk_registery.pop_back();
        }
    }
    std::cout << std::endl;
    std::cout << "ram: " << std::endl;
    for (ndn::Name e : _ram_registery) {
        std::cout << e << std::endl;
    }
    std::cout << std::endl;
    std::cout << "disk: " << std::endl;
    for (ndn::Name e : _disk_registery) {
        std::cout << e << std::endl;
    }
}

std::shared_ptr<ndn::Data> lru_cache::tryGet(ndn::Name name) {
    auto it = _entries.find(name);
    if (it != _entries.end()) {
        entry e = it->second;
        if (e.getData()) {
            auto it2 = _ram_registery.begin();
            while (*it2 != name) {
                ++it2;
            }
            if (e.getData()->getFreshnessPeriod().count() > 0) {
                _ram_registery.splice(_ram_registery.begin(), _ram_registery, it2);
                return e.getData();
            }
            _ram_registery.erase(it2);
            return nullptr;
        }
        e = entry::getFromDisk(name);
        if (e.getData()) {
            auto it2 = _disk_registery.begin();
            while (*it2 != name) {
                ++it2;
            }
            _disk_registery.erase(it2);
            insert(e.getData());
            if (e.getData()->getFreshnessPeriod().count() > 0) {
                return e.getData();
            }
            return nullptr;
        }
    }
    return nullptr;
}