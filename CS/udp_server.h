#pragma once

#include <boost/asio.hpp>
#include <boost/array.hpp>

#include <memory>
#include <string>
#include <queue>

#include "cs_cache.h"

class udp_server {
private:
    class udp_session : public std::enable_shared_from_this<udp_session> {
    public:
        udp_server &_udps;
        boost::asio::ip::udp::endpoint _endpoint_in;
        std::shared_ptr<boost::asio::ip::udp::socket>_socket_out;
        char _buffer[8800];

        udp_session(udp_server &udp_server);

        void process(size_t bytes_transferred);

        void receiveFromNext(const boost::system::error_code &err, size_t bytes_transferred);

        void sendToPrev(const boost::system::error_code &err, size_t bytes_transferred);

        void handle_sent(const boost::system::error_code &err, size_t bytes_transferred);
    };

    boost::asio::io_service &_ios;
    boost::asio::strand _strand;
    boost::asio::ip::udp::socket _socket_in/*, _socket_out*/;
    std::map<ndn::Name, std::list<boost::asio::ip::udp::endpoint>> _endpoints;
    std::queue<std::shared_ptr<boost::asio::ip::udp::socket>> _sockets;
    boost::asio::ip::udp::endpoint /*_endpoint_in,*/ _endpoint_out;
    cs_cache &_cs;

public:
    udp_server(boost::asio::io_service &ios, cs_cache &cs, std::string host, std::string port);

    ~udp_server();

    void receive();

    void handle_receive(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                        size_t bytes_transferred);

    void enqueue_response(std::shared_ptr<udp_session> const& session, size_t bytes_transferred);
};