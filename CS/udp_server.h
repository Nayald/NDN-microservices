#pragma once

#include <boost/asio.hpp>

class udp_server {
private:
    boost::asio::io_service *_ios;
    boost::asio::ip::udp::socket _socket;

    class udp_session {
        friend class udp_server;

    private:
        boost::asio::ip::udp::endpoint _endpoint;
        char _buffer[8800];

    public:
        static const size_t buffer_size = 8800;

        udp_session();

        ~udp_session();

        void process(size_t bytes_transferred);
    };

public:
    udp_server(boost::asio::io_service &ios);

    ~udp_server();

    void receive_session();

    void handle_session(std::shared_ptr<udp_session> session, const boost::system::error_code &err,
                        size_t bytes_transferred);
};

