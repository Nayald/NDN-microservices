#include "fib.h"

void Fib::purge(Node *node) {
    while(node->isUseless()) {
        Node* parent = node->getParent();
        parent->delChild(node->getName().get(-1));
        _entries.erase(node->getName());
        node = parent;
    }
}

Fib::Fib() {
    _entries.emplace(ndn::Name(), Node(ndn::Name(), nullptr));
    root = &_entries.at(ndn::Name());
}

Fib::~Fib() {

}

void Fib::insert(const ndn::Name &name, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) {
    if(_entries.find(name) == _entries.end()){
        Node *parent = root;
        for(int i = 0; i < name.size(); ++i) {
            if(parent->getChild(name.get(i)) == nullptr) {
                parent->addChild(name.get(i), &(_entries.emplace(std::piecewise_construct, std::forward_as_tuple(name.getPrefix(i + 1)), std::forward_as_tuple(name.getPrefix(i + 1), parent)).first->second));
            }
            parent = parent->getChild(name.get(i));
        }
    }
    _entries.at(name).addFace(socket);
}

void Fib::remove(const ndn::Name &name, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket){
    auto it = _entries.find(name);
    if(it != _entries.end()) {
        it->second.delFace(socket);
        if(it->second.isUseless()) {
            purge(&it->second);
        }
    }
}

void Fib::remove(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket){
    for(auto it = _entries.begin(); it != _entries.end(); ++it){
        it->second.delFace(socket);
        if(it->second.isUseless()) {
            purge(&it->second);
        }
    }
}

Node* Fib::findLongest(const ndn::Name &name) {
    auto it = _entries.find(name);
    if(it != _entries.end()){
        return &it->second;
    } else {
        Node *parent = root;
        for (int i = 0; i < name.size(); ++i) {
            Node *child = parent->getChild(name.get(i));
            if (child) {
                parent = child;
            } else {
                break;
            }
        }
        return parent;
    }
}

std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> Fib::findAllValidFaces(const ndn::Name &name) {
    std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> faces;
    auto it = _entries.find(name);
    if(it != _entries.end()){
        Node* node = &it->second;
        while(node != root) {
            faces.insert(node->getFaces().begin(), node->getFaces().end());
            node = node->getParent();
        }
        return faces;
    } else {
        Node *node = root;
        faces.insert(node->getFaces().begin(), node->getFaces().end());
        for (const auto& name_component : name) {
            if ((node = node->getChild(name_component)) != nullptr) {
                faces.insert(node->getFaces().begin(), node->getFaces().end());
            } else {
                break;
            }
        }
        return faces;
    }
}