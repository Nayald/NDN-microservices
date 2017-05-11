#include "pit.h"

Pit::~Pit() {

}

bool Pit::insert(ndn::Interest &interest, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    auto it =_entries.find(interest.getName());
    if(it != _entries.end()){
        return it->second.addFace(interest, socket);
    }else {
        _entries.emplace(std::piecewise_construct, std::forward_as_tuple(interest.getName()), std::forward_as_tuple(interest, socket));
        return true;
    }
}

void Pit::remove(const ndn::Name &name){
    _entries.erase(name);
}

void Pit::remove(std::shared_ptr<boost::asio::ip::tcp::socket> socket_in){
    auto it = _entries.begin();
    while(it != _entries.end()){
        it->second.delFace(socket_in);
        if (it->second.getFaces().empty()){
            it = _entries.erase(it);
        } else {
            ++it;
        }
    }
}

Node Pit::get(const ndn::Name &name) {
    auto it = _entries.find(name);
    return it != _entries.end() ? it->second : Node();
}