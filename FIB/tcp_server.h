#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <string>
#include <set>

#include "rapidjson/document.h"

#include "fib.h"

class Tcp_server {
private:
    class Tcp_client_session : public std::enable_shared_from_this<Tcp_client_session> {
    public:
        Tcp_server &_tcps;
        std::shared_ptr<boost::asio::ip::tcp::socket> _socket_client;
        char _buffer[8800];
        std::string interest_flow;

        Tcp_client_session(Tcp_server &tcps, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket_in);

        void process();

        void sendToNext(const boost::system::error_code &err, size_t bytes_transferred);
    };

    class Tcp_server_session : public std::enable_shared_from_this<Tcp_server_session> {
    public:
        Tcp_server &_tcps;
        std::shared_ptr<boost::asio::ip::tcp::socket> _socket_server;
        char _buffer[8800];
        std::string data_flow;

        Tcp_server_session(Tcp_server &tcps, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket_in);

        void fibRegistration();

        void handleFibRegistration(const boost::system::error_code &err, size_t bytes_transferred);

        void process();

        void sendToPrev(const boost::system::error_code &err, size_t bytes_transferred);
    };

    enum action_type {
        INSERT,
        UPDATE,
        DELETE,
        LIST
    };

    const std::map<std::string, action_type> actions =
            {
                    {"insert", action_type::INSERT},
                    {"update", action_type::UPDATE},
                    {"delete", action_type::DELETE},
                    {"list", action_type::LIST}
            };

    enum update_method_type {
        ADD,
        REMOVE
    };

    const std::map<std::string, update_method_type> update_methods =
            {
                    {"add", update_method_type::ADD},
                    {"remove", update_method_type::REMOVE}
            };

    boost::asio::ip::tcp::resolver _resolver;
    char _command_buffer[65536];
    boost::asio::ip::udp::socket _command_socket;
    boost::asio::ip::udp::endpoint _remote_udp_endpoint;
    int _socket_id;
    std::map<int, std::shared_ptr<boost::asio::ip::tcp::socket>> _command_sockets;
    boost::asio::ip::tcp::acceptor _client_acceptor, _server_acceptor;
    std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> _client_sockets;
    Fib _fib;

public:
    Tcp_server(boost::asio::io_service &ios, std::string host, std::string port);

    ~Tcp_server();

    void process();

    void commandHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void commandInsert(const rapidjson::Document &document);

    void commandUpdate(const rapidjson::Document &document);

    void commandDelete(const rapidjson::Document &document);

    void commandList(const rapidjson::Document &document);

    void accept_client();

    void accept_server();

    template<class T>
    static void findPacket(uint8_t packetDelimiter, std::string &stream, std::vector<T> &structure);
};