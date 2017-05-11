#pragma once

#include <ndn-cxx/interest.hpp>

#include <boost/asio.hpp>

#include <memory>
#include <map>
#include <set>

#include "node.h"

class Pit {
private:
    std::map<ndn::Name, Node> _entries;

public:
    virtual ~Pit();

    virtual bool insert(ndn::Interest &interest, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    virtual void remove(const ndn::Name &name);

    virtual void remove(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    virtual Node get(const ndn::Name &name);
};

