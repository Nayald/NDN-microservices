#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <queue>

#include "pit.h"

class Tcp_server {
private:
    class Tcp_session : public std::enable_shared_from_this<Tcp_session> {
    public:
        Tcp_server *_tcps;
        std::shared_ptr<boost::asio::ip::tcp::socket> _socket_in;
        char _buffer_in[8800];
        std::string interest_flow;

        Tcp_session(Tcp_server *tcps, std::shared_ptr<boost::asio::ip::tcp::socket> socket_in);

        void process();

        void sendToNext(const boost::system::error_code &err, size_t bytes_transferred);
    };

    boost::asio::ip::tcp::acceptor _acceptor;
    boost::asio::ip::tcp::endpoint _endpoint_out;
    boost::asio::ip::tcp::socket _socket_out;
    char _buffer_out[8800];
    std::string data_flow;

    Pit _pit;

public:
    Tcp_server(boost::asio::io_service &ios, std::string host, std::string port);

    ~Tcp_server();

    void accept();

    void receiveFromNext();

    void sendToPrev(const boost::system::error_code &err, size_t bytes_transferred);

    template<class T>
    static void findPacket(uint8_t packetDelimiter, std::string &stream, std::vector<T> &structure);
};