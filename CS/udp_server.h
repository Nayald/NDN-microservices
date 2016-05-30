#pragma once

#include <boost/asio.hpp>

#include <memory>

#include "cs_cache.h"

class udp_server {
private:
    class udp_session {
    public:
        udp_server &_udp_server;
        boost::asio::ip::udp::endpoint _endpoint;
        char _buffer[8800];

        static const size_t buffer_size = 8800;

        udp_session(udp_server &udp_server);

        ~udp_session();

        void process(size_t bytes_transferred);

        void handle_send(const boost::system::error_code &err, size_t bytes_transferred);
    };

    boost::asio::ip::udp::socket _socket;
    cs_cache &_cs;

public:
    udp_server(boost::asio::io_service &ios, cs_cache &cs);

    ~udp_server();

    void receive();

    void handle_receive(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                        size_t bytes_transferred);
};

