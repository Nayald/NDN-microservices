#pragma once

#include <ndn-cxx/name.hpp>

#include <boost/asio.hpp>
#include <boost/chrono.hpp>

#include <sstream>
#include <memory>
#include <map>
#include <set>

class Node {
private:
    ndn::Name _name;
    Node *_parent;
    std::map<ndn::Name::Component, Node*> _children;
    std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> _faces;

public:
    Node(const ndn::Name &name, Node *parent);

    ~Node();

    const ndn::Name& getName() const;

    Node* getParent() const;

    Node* getChild(const ndn::Name::Component &name_component) const;

    void addChild(const ndn::Name::Component &name_component, Node *node);

    void delChild(const ndn::Name::Component &name_component);

    const std::set<std::shared_ptr<boost::asio::ip::tcp::socket>>& getFaces() const;

    void addFace(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);

    bool delFace(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);

    bool isUseless() const;

    std::string toString(){
        std::string output = _name.toUri() + " (children_count=" + std::to_string(_children.size()) + ", faces_count=" + std::to_string(_faces.size()) + ")\n";
        for(auto it = _children.begin(); it != _children.end(); ++it) {
            output += it->second->toString();
        }
        return output;
    }

    std::string toJson() {
        std::stringstream output;
        output << "{\"name\":\"" << _name << "\",\"endpoints\":[";
        bool first_endpoint = true;
        for (const auto &face : _faces) {
            if (first_endpoint) {
                first_endpoint = false;
            } else {
                output << ",";
            }
            output << "\"" << face->remote_endpoint() << "\"";
        }
        output << "],\"children\":[";
        bool first_child = true;
        for (const auto &child : _children) {
            if (first_child) {
                first_child = false;
            } else {
                output << ",";
            }
            output << child.second->toJson();
        }
        output << "]}";
        return output.str();
    }
};

