#pragma once

#include <ndn-cxx/interest.hpp>

#include <boost/asio.hpp>

#include <memory>
#include <map>
#include <set>

#include "node.h"

class Fib {
private:
    std::map<ndn::Name, Node> _entries;

    void purge(Node *node);

public:
    Node* root;

    Fib();

    ~Fib();

    void insert(const ndn::Name &name, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);

    void remove(const ndn::Name &name, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);

    void remove(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);

    Node* findLongest(const ndn::Name &name);

    std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> findAllValidFaces(const ndn::Name &name);

    std::string toString(){
        return root->toString();
    }

    std::string toJson(){
        return "{\"table\":\"fib\",\"root\":" + root->toJson() + "}";
    }
};

