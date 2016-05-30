#include "lru_cache.h"

lru_cache::lru_cache(uint32_t ram_size, uint32_t disk_size) {
    _ram_size = ram_size;//nextPowerOf2(ram_size);
    _disk_size = disk_size;//nextPowerOf2(disk_size);
}

lru_cache::~lru_cache() {

}

void lru_cache::insert(ndn::Data data) {
    if (data.getFreshnessPeriod().count() > 0) {
        //_entries[data.getName()] = cs_cache::node(data.shared_from_this());
        _entries.emplace(data.getName(), linked_list::node(data.shared_from_this()));
        _ram_registery.push(&_entries.at(data.getName()));
        checkSize();
    }
    /*std::cout << "ram: " << std::endl;
    for (ndn::Name e : _ram_registery) {
        std::cout << e << std::endl;
    }
    std::cout << std::endl;
    std::cout << "disk: " << std::endl;
    for (ndn::Name e : _disk_registery) {
        std::cout << e << std::endl;
    }*/
}

void lru_cache::insert(std::shared_ptr<ndn::Data> data_ptr) {
    if (data_ptr->getFreshnessPeriod().count() > 0) {
        //_entries[data_ptr->getName()] = cs_cache::node(data_ptr);
        _entries.emplace(data_ptr->getName(), linked_list::node(data_ptr));
        _ram_registery.push(&_entries.at(data_ptr->getName()));
        checkSize();
    }
    //std::cout << "ram: " << _ram_registery.size() << std::endl;
    //cs_cache::node *node = _ram_registery.getHead();
    /*while(node){
        std::cout << node->getValue().getData()->getName() << std::endl;
        node = node->getNext();
    }*/
    //std::cout << "disk: " << _disk_registery.size() << std::endl;
    //std::cout << std::endl;
}

std::shared_ptr<ndn::Data> lru_cache::tryGet(ndn::Name name) {
    auto it = _entries.find(name);
    if (it != _entries.end()) {
        linked_list::node node = it->second;
        if (node.getValue().getData()) {
            if (node.getValue().isValid()) {
                _ram_registery.push(_ram_registery.remove(&node));
                return node.getValue().getData();
            } else {
                _ram_registery.remove(&node);
                _entries.erase(name);
                return nullptr;
            }
        }
        node.setValue(entry::getFromDisk(name));
        if (node.getValue().getData()) {
            if (node.getValue().isValid()) {
                _ram_registery.push(_disk_registery.remove(&node));
                checkSize();
                return node.getValue().getData();
            } else {
                _disk_registery.remove(&node);
                _entries.erase(name);
                return nullptr;
            }
        }
    }
    return nullptr;
}

void lru_cache::checkSize(){
    if (_ram_registery.size() > _ram_size) {
        if (_disk_size > 0) {
            linked_list::node *node_ptr = _ram_registery.pop();
            node_ptr->getValue().storeToDisk();
            node_ptr->setValue(entry());
            _disk_registery.push(node_ptr);
        } else {
            _entries.erase(_ram_registery.pop()->getValue().getData()->getName());
        }
    }
    if (_disk_registery.size() > _disk_size) {
        ndn::Name name = _disk_registery.pop()->getValue().getData()->getName();
        _entries.erase(name);
        entry::removeFromDisk(name);
    }
}