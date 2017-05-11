#include "node.h"

Node::Node(const ndn::Name &name, Node *parent)
        : _name(name)
        , _parent(parent) {

}

Node::~Node() {

}

const ndn::Name& Node::getName() const {
    return _name;
}

Node* Node::getParent() const {
    return _parent;
}

Node* Node::getChild(const ndn::Name::Component &name_component) const {
    auto it = _children.find(name_component);
    return it != _children.end() ? it->second : nullptr;
}

void Node::addChild(const ndn::Name::Component &name_component, Node *node) {
    _children.emplace(name_component, node);
}

void Node::delChild(const ndn::Name::Component &name_component) {
    _children.erase(name_component);
}

const std::set<std::shared_ptr<boost::asio::ip::tcp::socket>>& Node::getFaces() const {
    return _faces;
}

void Node::addFace(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) {
    _faces.emplace(socket);
}

bool Node::delFace(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) {
    auto it = _faces.find(socket);
    if (it != _faces.end()) {
        _faces.erase(socket);
        return true;
    }else {
        return false;
    }
}

bool Node::isUseless() const {
    return _faces.empty() && _children.empty() && _parent != nullptr;
}