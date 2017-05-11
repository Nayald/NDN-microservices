#include "node.h"

Node::Node(){

}

Node::Node(ndn::Interest &interest, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
        : _name(interest.getName())
        , _lastUpdate(boost::chrono::steady_clock::now())
        , _keepUntil(_lastUpdate + interest.getInterestLifetime()) {
    _faces.emplace(socket);
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

const std::set<std::shared_ptr<boost::asio::ip::tcp::socket>>& Node::getFaces() {
    return _faces;
}

bool Node::addFace(ndn::Interest &interest, std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    _faces.emplace(socket);
    auto tmp = _lastUpdate;
    _lastUpdate = boost::chrono::steady_clock::now();
    _keepUntil = _lastUpdate + interest.getInterestLifetime();
    return tmp + boost::chrono::milliseconds(250) < _lastUpdate;
}

void Node::delFace(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    _faces.erase(socket);
}

bool Node::isValid() {
    return _keepUntil > boost::chrono::steady_clock::now();
}














