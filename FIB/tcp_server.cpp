#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <boost/bind.hpp>

#include "tcp_server.h"

Tcp_server::Tcp_client_session::Tcp_client_session(Tcp_server &tcps, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket_in)
        : _tcps(tcps)
        , _socket_client(socket_in) {
    std::cout << "client session start by " << _socket_client->remote_endpoint() << std::endl;
}

void Tcp_server::Tcp_client_session::process() {
    _socket_client->async_read_some(boost::asio::buffer(_buffer, 8800),
                                boost::bind(&Tcp_client_session::sendToNext,
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
}

void Tcp_server::Tcp_client_session::sendToNext(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        std::vector<ndn::Interest> interests;
        interest_flow.append(_buffer, bytes_transferred);

        //find the interests in the stream
        findPacket(0x05, interest_flow, interests);

        for(auto& interest : interests) {
            /*Node *n = _tcps._fib.findLongest(interest.getName());
            while (n != nullptr) {
                for (const auto& face : n->getFaces()) {
                    try {
                        boost::asio::write(*face, boost::asio::buffer(interest.wireEncode().wire(), interest.wireEncode().size()));
                    } catch (std::exception &e) {
                        std::cout << e.what() << std::endl;
                    }
                }
                n = n->getParent();
            }*/
            for(const auto& face : _tcps._fib.findAllValidFaces(interest.getName())) {
                try {
                    boost::asio::write(*face, boost::asio::buffer(interest.wireEncode().wire(), interest.wireEncode().size()));
                } catch (std::exception &e) {
                    std::cout << e.what() << std::endl;
                }
            }
        }
        process();
    } else {
        _tcps._client_sockets.erase(_socket_client);
        std::cout << "a client leave" << std::endl;
    }
}

//----------------------------------------------------------------------------------------------------------------------

Tcp_server::Tcp_server_session::Tcp_server_session(Tcp_server &tcps, const std::shared_ptr<boost::asio::ip::tcp::socket> &socket_in)
        : _tcps(tcps)
        , _socket_server(socket_in) {
    std::cout << "server session start by " << _socket_server->remote_endpoint() << std::endl;
}

void Tcp_server::Tcp_server_session::fibRegistration() {
    _socket_server->async_read_some(boost::asio::buffer(_buffer, 8800),
                                boost::bind(&Tcp_server_session::handleFibRegistration,
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
}

void Tcp_server::Tcp_server_session::handleFibRegistration(const boost::system::error_code &err, size_t bytes_transferred){
    try{
        ndn::Interest interest(ndn::Block(_buffer, bytes_transferred));
        //std::cout << interest.getName().get(4).wire() << std::endl;
        std::string s((char*)interest.getName().get(4).wire(), interest.getName().get(4).size());
        while(s[0] != 0x07){
            switch(s[1]){
                case 0xFF:
                    s.erase(0, 10);
                    break;
                case 0xFE:
                    s.erase(0, 6);
                    break;
                case 0xFD:
                    s.erase(0, 4);
                    break;
                default:
                    s.erase(0, 2);
                    break;
            }
        }
        ndn::Name name(ndn::Block(s.c_str(), s.size()));
        std::cout << name << std::endl;
        ndn::Data d(interest.getName());
        uint8_t content[44] = {0x65, 0x2a, 0x66, 0x01, 0xc8, 0x67, 0x07, 0x53, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0x68, 0x1c, 0x07, 0x0d, 0x08, 0x03, 0x63, 0x6f, 0x6d, 0x08, 0x06, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x69, 0x02, 0x01, 0x0d, 0x6f, 0x01, 0x00, 0x6a, 0x01, 0x00, 0x6c, 0x01, 0x01};
        d.setContent(content, 44);
        d.setFreshnessPeriod(boost::chrono::milliseconds(-1));
        ndn::security::KeyChain k;
        k.sign(d);
        boost::asio::write(*_socket_server, boost::asio::buffer(d.wireEncode().wire(), d.wireEncode().size()));
        _tcps._fib.insert(name, _socket_server);
        process();
    }catch (std::exception &e){
        std::cout << e.what() << std::endl;
    }
}

void Tcp_server::Tcp_server_session::process(){
    _socket_server->async_read_some(boost::asio::buffer(_buffer, 8800),
                                    boost::bind(&Tcp_server_session::sendToPrev,
                                                shared_from_this(),
                                                boost::asio::placeholders::error,
                                                boost::asio::placeholders::bytes_transferred));
}

void Tcp_server::Tcp_server_session::sendToPrev(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        std::vector<ndn::Data> datas;
        data_flow.append(_buffer, bytes_transferred);

        //find the interests in the stream
        findPacket(0x06, data_flow, datas);

        std::string forward_buffer;
        for(const auto& data : datas){
            //std::cout << data.getName() << std::endl;
            forward_buffer.append(reinterpret_cast<const char *>(data.wireEncode().wire()),
                                  data.wireEncode().size());
        }

        //forward the interest(s) to the next hop
        for(const auto& face : _tcps._client_sockets){
            try {
                boost::asio::write(*face, boost::asio::buffer(forward_buffer));
            } catch(std::exception &e){
                std::cout << e.what() << std::endl;
            }
        }
        process();
    } else {
        _tcps._fib.remove(_socket_server);
        std::cout << "a server leave" << std::endl;
    }
}

//----------------------------------------------------------------------------------------------------------------------

Tcp_server::Tcp_server(boost::asio::io_service &ios, std::string host, std::string port)
        : _resolver(ios)
        , _command_socket(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 6363))
        , _client_acceptor(ios, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 6362))
        , _server_acceptor(ios, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 6363)) {
    process();
    accept_client();
    accept_server();
}

Tcp_server::~Tcp_server() { }

void Tcp_server::process() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536),
                                       _remote_udp_endpoint,
                                       boost::bind(&Tcp_server::commandHandler,
                                                   this,
                                                   boost::asio::placeholders::error,
                                                   boost::asio::placeholders::bytes_transferred));
}

void Tcp_server::commandHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    if(!err) {
        try {
            rapidjson::Document document;
            document.Parse(_command_buffer, bytes_transferred);
            if(!document.HasParseError()){
                if(document.HasMember("action") && document["action"].IsString()){
                    auto it = actions.find(document["action"].GetString());
                    if(it != actions.end()) {
                        switch (it->second) {
                            case INSERT:
                                commandInsert(document);
                                break;
                            case UPDATE:
                                commandUpdate(document);
                                break;
                            case DELETE:
                                commandDelete(document);
                                break;
                            case LIST:
                                commandList(document);
                                break;
                        }
                    }
                } else{
                    std::string reponse = "{\"status\":\"fail\", \"reason\":\"action not provided or not implemented\"}";
                    _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
                }
            } else {
                //std::string error_extract(&_command_buffer[document.GetErrorOffset()], std::min(32ul, bytes_transferred - document.GetErrorOffset()));
                //std::string reponse = "{\"status\":\"fail\", \"reason\":\"error while parsing\"}";
                //_command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
            }
        } catch(const std::exception &e) {
            std::cout << e.what() << std::endl;
        }
        process();
    } else {
        std::cerr << "command socket error !" << std::endl;
    }
}

void Tcp_server::commandInsert(const rapidjson::Document &document) {
    if (document.HasMember("address") && document.HasMember("port") && document.HasMember("routes")
        && document["address"].IsString() && document["port"].IsString() && document["routes"].IsArray()) {
        std::string address = document["address"].GetString();
        std::string port = document["port"].GetString();
        std::vector<ndn::Name> routes;
        for (const auto &route : document["routes"].GetArray()) {
            if (route.IsString()) {
                routes.emplace_back(route.GetString());
            }
        }
        if (!routes.empty()) {
            auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_command_socket.get_io_service());
            _resolver.async_resolve({address, port}, [this, socket, routes](const boost::system::error_code &err, boost::asio::ip::tcp::resolver::iterator it) {
                if (!err && it != boost::asio::ip::tcp::resolver::iterator()) {
                    boost::asio::async_connect(*socket, it, [this, socket, routes](const boost::system::error_code &err2, boost::asio::ip::tcp::resolver::iterator it2) {
                        if (!err2 && it2 != boost::asio::ip::tcp::resolver::iterator()) {
                            _command_sockets.emplace(++_socket_id, socket);
                            std::make_shared<Tcp_server_session>(*this, socket)->process();
                            for (const auto &route : routes) {
                                _fib.insert(route, socket);
                            }
                            std::string reponse = "{\"status\":\"success\", \"id\":" + std::to_string(_socket_id) + "}";
                            _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
                        } else {
                            std::string reponse = "{\"status\":\"fail\", \"reason\":\"connection fail\"}";
                            _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
                        }
                    });
                } else {
                    std::string reponse = "{\"status\":\"fail\", \"reason\":\"host not found\"}";
                    _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
                }
            });
        }
    }
}

void Tcp_server::commandUpdate(const rapidjson::Document &document) {
    if (document.HasMember("method") && document.HasMember("id") && document.HasMember("routes")
        && document["method"].IsString() && document["id"].IsInt() && document["routes"].IsArray()) {
        auto it = update_methods.find(document["method"].GetString());
        if (it != update_methods.end()) {
            auto it2 = _command_sockets.find(document["id"].GetInt());
            if (it2 != _command_sockets.end()) {
                switch (it->second) {
                    case ADD:
                        for (const auto &route : document["routes"].GetArray()) {
                            if (route.IsString()) {
                                _fib.insert(route.GetString(), it2->second);
                            }
                        }
                        break;
                    case REMOVE:
                        for (const auto &route : document["routes"].GetArray()) {
                            if (route.IsString()) {
                                _fib.remove(route.GetString(), it2->second);
                            }
                        }

                        break;
                }
                std::string reponse = "{\"status\":\"success\"}";
                _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
            } else {
                std::string reponse = "{\"status\":\"fail\", \"reason\":\"id not found\"}";
                _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
            }
        } else {
            std::string reponse = "{\"status\":\"fail\", \"reason\":\"method not implemented\"}";
            _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
        }
    }
}

void Tcp_server::commandDelete(const rapidjson::Document &document){
    if(document.HasMember("id") && document["id"].IsInt()){
        auto it = _command_sockets.find(document["id"].GetInt());
        if(it != _command_sockets.end()){
            it->second->close();
            _command_sockets.erase(it);
            std::string reponse = "{\"status\":\"success\"}";
            _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
        } else {
            std::string reponse = "{\"status\":\"fail\", \"reason\":\"id not found\"}";
            _command_socket.send_to(boost::asio::buffer(reponse), _remote_udp_endpoint);
        }
    }
}

void Tcp_server::commandList(const rapidjson::Document &document){
    _command_socket.send_to(boost::asio::buffer(_fib.toJson()), _remote_udp_endpoint);
}

void Tcp_server::accept_client() {
    auto socket_client = std::make_shared<boost::asio::ip::tcp::socket>(_client_acceptor.get_io_service());
    _client_acceptor.async_accept(*socket_client, [this, socket_client](const boost::system::error_code &err) {
        if (!err) {
            std::make_shared<Tcp_client_session>(*this, socket_client)->process();
            _client_sockets.emplace(socket_client);
        }
        accept_client();
    });
}

void Tcp_server::accept_server() {
    auto socket_server = std::make_shared<boost::asio::ip::tcp::socket>(_server_acceptor.get_io_service());
    _server_acceptor.async_accept(*socket_server, [this, socket_server](const boost::system::error_code &err) {
        if (!err) {
            std::make_shared<Tcp_server_session>(*this, socket_server)->fibRegistration();
        }
        accept_server();
    });
}

//----------------------------------------------------------------------------------------------------------------------

template<class T>
void Tcp_server::findPacket(uint8_t packetDelimiter, std::string &stream, std::vector<T> &structure){
    //find as much as possible packets in the stream
    try {
        do {
            //packet start with the delimiter (for ndn 0x05 or 0x06) so we remove any starting bytes that is not one of them
            stream.erase(0, stream.find(packetDelimiter));

            //in tlv spec the size doesn't count the first byte and itself (at least 1 byte)
            uint64_t size = 2;

            //select the lenght of the size, according to tlv format (1, 2, 4 or 8 bytes to read)
            switch (static_cast<uint8_t>(stream[1])) {
                default:
                    size += static_cast<uint8_t>(stream[1]);
                    break;
                case 0xFD:
                    size += static_cast<uint16_t>(stream[2]) << 8;
                    size += static_cast<uint16_t>(stream[3]) + 2;
                    break;
                case 0xFE:
                    size += static_cast<uint32_t>(stream[2]) << 24;
                    size += static_cast<uint32_t>(stream[3]) << 16;
                    size += static_cast<uint32_t>(stream[4]) << 8;
                    size += static_cast<uint32_t>(stream[5]) + 4;
                    break;
                case 0xFF:
                    size += static_cast<uint64_t>(stream[2]) << 56;
                    size += static_cast<uint64_t>(stream[3]) << 48;
                    size += static_cast<uint64_t>(stream[4]) << 40;
                    size += static_cast<uint64_t>(stream[5]) << 32;
                    size += static_cast<uint64_t>(stream[6]) << 24;
                    size += static_cast<uint64_t>(stream[7]) << 16;
                    size += static_cast<uint64_t>(stream[8]) << 8;
                    size += static_cast<uint64_t>(stream[9]) + 8;
                    break;
            }
            //std::cerr << size << "/" << stream.size() << std::endl;

            //check if the stream have enough bytes else wait for more data in the stream
            if(size > ndn::MAX_NDN_PACKET_SIZE) {
                stream.erase(0, 1);
            } else if (size <= stream.size()) {
                //try to build the packet object then remove the read bytes from stream if fail remove the first byte in order to find the next delimiter
                try {
                    structure.emplace_back(ndn::Block(stream.c_str(), size));
                    stream.erase(0, size);
                } catch (std::exception &e) {
                    std::cerr << e.what() << std::endl;
                    stream.erase(0, 1);
                }
            } else {
                break;
            }
        } while (!stream.empty());
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}