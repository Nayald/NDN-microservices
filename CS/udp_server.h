#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <set>

#include "cs_cache.h"

class udp_server;

class udp_session;

class udp_server {
private:
    class udp_session : public std::enable_shared_from_this<udp_session> {
    public:
        udp_server &_udps;
        char _buffer[8800];

        static const size_t buffer_size = 8800;

        udp_session(udp_server &udp_server);

        ~udp_session();

        void process(size_t bytes_transferred);

        void receiveFromNext(const boost::system::error_code &err, size_t bytes_transferred);

        void sendToPrev(const boost::system::error_code &err, size_t bytes_transferred);
    };

    boost::asio::ip::udp::socket _socket_in, _socket_out;
    boost::asio::ip::udp::endpoint _endpoint_in, _endpoint_out;
    boost::asio::strand strand;
    cs_cache &_cs;
    std::set<std::shared_ptr<udp_session>> set;

public:
    udp_server(boost::asio::io_service &ios, cs_cache &cs, std::string host, std::string port);

    ~udp_server();

    void receive();

    void handle_receive(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                        size_t bytes_transferred);
};