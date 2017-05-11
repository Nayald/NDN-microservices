#pragma once

#include <ndn-cxx/interest.hpp>

#include <boost/asio.hpp>
#include <boost/chrono.hpp>

#include <memory>
#include <set>

class Node {
private:
    ndn::Name _name;
    Node *_parent;
    std::map<ndn::Name::Component, Node*> _children;
    std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> _faces;
    boost::chrono::steady_clock::time_point _lastUpdate, _keepUntil;

public:
    Node();

    Node(ndn::Interest &interest, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    ~Node();

    const ndn::Name& getName() const;

    Node* getParent() const;

    Node* getChild(const ndn::Name::Component &name_component) const;

    void addChild(const ndn::Name::Component &name_component, Node *node);

    void delChild(const ndn::Name::Component &name_component);

    const std::set<std::shared_ptr<boost::asio::ip::tcp::socket>>& getFaces();

    bool addFace(ndn::Interest &interest, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    void delFace(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    bool isValid();
};