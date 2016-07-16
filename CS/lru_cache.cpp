#include "lru_cache.h"

lru_cache::lru_cache(uint32_t ram_size, uint32_t disk_size) {
    _ram_current_size = 0;
    _disk_current_size = 0;
    _ram_max_size = ram_size;//nextPowerOf2(ram_size);
    _disk_max_size = disk_size;//nextPowerOf2(disk_size);
}

lru_cache::~lru_cache() {

}

void lru_cache::insert(ndn::Data &data) {
    if (data.getFreshnessPeriod().count() > 0) {
        _ram_registery.emplace_front(data);
        ++_ram_current_size;
        _entries[data.getName()] = _ram_registery.begin();
        checkSize();
    }
    //std::cerr << toString() << std::endl;
}

void lru_cache::insert(std::shared_ptr<ndn::Data> data_ptr) {
    if (data_ptr->getFreshnessPeriod().count() > 0) {
        //_entries[data_ptr->getName()] = cs_cache::node(data_ptr);
        _ram_registery.emplace_front(data_ptr);
        ++_ram_current_size;
        _entries[data_ptr->getName()] = _ram_registery.begin();
        checkSize();
    }
    //std::cerr << toString() << std::endl;
}

std::shared_ptr<ndn::Data> lru_cache::tryGet(ndn::Name name) {
    auto it = _entries.find(name);
    if (it != _entries.end()) {
        auto it2 = it->second;
        if (it2->getData()) {
            if (it2->isValid()) {
                //_ram_registery.erase(it2);
                //_ram_registery.insert(it2,_ram_registery.begin());
                _ram_registery.splice(_ram_registery.begin(), _ram_registery, it2);
                return it2->getData();
            } else {
                _ram_registery.erase(it2);
                _entries.erase(name);
                return nullptr;
            }
        }
        *it2 = entry::getFromDisk(name);
        if (it2->getData()) {
            if (it2->isValid()) {
                //_ram_registery.push(_disk_registery.remove(&node));
                _ram_registery.splice(_ram_registery.begin(), _disk_registery, it2);
                checkSize();
                return it2->getData();
            } else {
                _disk_registery.erase(it2);
                _entries.erase(name);
                return nullptr;
            }
        }
    }
    return nullptr;
}

void lru_cache::checkSize() {
    if (_ram_current_size > _ram_max_size) {
        if (_disk_max_size > 0) {
            entry e = _ram_registery.back();
            e.storeToDisk();
            e = entry();
            _disk_registery.splice(_disk_registery.begin(), _ram_registery, --_ram_registery.end());
            --_ram_current_size;
            ++_disk_current_size;
        } else {
            _entries.erase(_ram_registery.rbegin()->getData()->getName());
            _ram_registery.pop_back();
            --_ram_current_size;
            return;
        }
    }
    if (_disk_current_size > _disk_max_size) {
        ndn::Name name = _disk_registery.rbegin()->getData()->getName();
        _entries.erase(name);
        _disk_registery.pop_back();
        entry::removeFromDisk(name);
        --_disk_current_size;
    }
}