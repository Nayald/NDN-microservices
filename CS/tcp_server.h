#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <queue>

#include "cs_cache.h"

class tcp_server {
private:
    class tcp_session : public std::enable_shared_from_this<tcp_session> {
    public:
        tcp_server *_tcps;
        boost::asio::ip::tcp::socket _socket_in, _socket_out;
        char _buffer_in[8800];
        char _buffer_out[8800];
        std::string interest_flow;
        std::string data_flow;

        tcp_session(tcp_server *tcps, boost::asio::ip::tcp::socket socket_in);

        void process(boost::asio::ip::tcp::endpoint endpoint_out);

        void receiveFromPrev(const boost::system::error_code &err);

        void sendToNext(const boost::system::error_code &err, size_t bytes_transferred);

        void receiveFromNext(const boost::system::error_code &err);

        void sendToPrev(const boost::system::error_code &err, size_t bytes_transferred);

        template <class T>
        void findPacket(uint8_t packetDelimiter, std::string &stream, std::vector<T> &structure);
    };

    boost::asio::ip::tcp::acceptor _acceptor;
    boost::asio::ip::tcp::socket _socket_in;
    boost::asio::ip::tcp::endpoint _endpoint_out;
    cs_cache &_cs;

public:
    tcp_server(boost::asio::io_service &ios, cs_cache &cs, std::string host, std::string port);

    ~tcp_server();

    void accept();
};