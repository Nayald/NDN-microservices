#include "packet_dispather.h"

#include "ndn-cxx/security/key-chain.hpp"

#include <boost/bind.hpp>

#include "network/tcp_master_face.h"
#include "network/tcp_face.h"
#include "network/udp_master_face.h"
#include "network/udp_face.h"
#include "log/logger.h"

size_t PacketDispatcher::Session::session_count = 0;

PacketDispatcher::Session::Session(PacketDispatcher &packet_dispatcher, boost::asio::ip::tcp::socket&& socket)
        : _packet_dispatcher(packet_dispatcher), _session_id(++session_count) {
    std::cout << "new session with ID = " << _session_id;
    _bidirectionnal_face = std::make_shared<TcpFace>(std::move(socket));
    if(packet_dispatcher._consumer_layer == "udp") {
        _consumer_face = std::make_shared<UdpFace>(_packet_dispatcher._ios, _packet_dispatcher._consumer_remote_ip, _packet_dispatcher._consumer_remote_port);
    } else {
        _consumer_face = std::make_shared<TcpFace>(_packet_dispatcher._ios, _packet_dispatcher._consumer_remote_ip, _packet_dispatcher._consumer_remote_port);
    }
    if(packet_dispatcher._producer_layer == "udp") {
        _producer_face = std::make_shared<UdpFace>(_packet_dispatcher._ios, _packet_dispatcher._producer_remote_ip, _packet_dispatcher._producer_remote_port);
    } else {
        _producer_face = std::make_shared<TcpFace>(_packet_dispatcher._ios, _packet_dispatcher._producer_remote_ip, _packet_dispatcher._producer_remote_port);
    }
    std::cout << " connected to " << _packet_dispatcher._consumer_remote_ip << ":" << _packet_dispatcher._consumer_remote_port << " and "
              << _packet_dispatcher._producer_remote_ip << ":" << _packet_dispatcher._producer_remote_port << std::endl;
}

PacketDispatcher::Session::~Session() {
    std::cout << "session with ID = " << _session_id << " destroyed" << std::endl;
}

void PacketDispatcher::Session::start() {
    _bidirectionnal_face->open(boost::bind(&PacketDispatcher::Session::onInterest, this, _1, _2),
                               boost::bind(&PacketDispatcher::Session::onData, this, _1, _2),
                               boost::bind(&PacketDispatcher::Session::onFaceError, this, _1));
    _consumer_face->open(boost::bind(&PacketDispatcher::Session::onInterest2, this, _1, _2),
                         boost::bind(&PacketDispatcher::Session::onData2, this, _1, _2),
                         boost::bind(&PacketDispatcher::Session::onFaceError, this, _1));
    _producer_face->open(boost::bind(&PacketDispatcher::Session::onInterest2, this, _1, _2),
                         boost::bind(&PacketDispatcher::Session::onData2, this, _1, _2),
                         boost::bind(&PacketDispatcher::Session::onFaceError, this, _1));
}

void PacketDispatcher::Session::stop() {
    _bidirectionnal_face->close();
    _consumer_face->close();
    _producer_face->close();
}

void PacketDispatcher::Session::onInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest) {
    static const ndn::Name localhost("/localhost");
    static const ndn::Name localhop("/localhop");
    (localhost.isPrefixOf(interest.getName()) || localhop.isPrefixOf(interest.getName()) ? _producer_face : _consumer_face)->send(interest);
}

void PacketDispatcher::Session::onData(const std::shared_ptr<Face> &face, const ndn::Data &data) {
    _producer_face->send(data);
}

void PacketDispatcher::Session::onInterest2(const std::shared_ptr<Face> &face, const ndn::Interest &interest) {
    _bidirectionnal_face->send(interest);
}

void PacketDispatcher::Session::onData2(const std::shared_ptr<Face> &face, const ndn::Data &data) {
    _bidirectionnal_face->send(data);
}

void PacketDispatcher::Session::onFaceError(const std::shared_ptr<Face> &egress_face) {
    stop();
}


PacketDispatcher::PacketDispatcher()
        : Module(1)
        , _acceptor(_ios, {{}, 6360})
        , _acceptor_socket(_ios)
        , _command_socket(_ios, {{}, 10002}){

}

void PacketDispatcher::run() {
    commandRead();
    accept();
}


void PacketDispatcher::accept() {
    _acceptor.async_accept(_acceptor_socket, boost::bind(&PacketDispatcher::acceptHandler, this, _1));
}

void PacketDispatcher::acceptHandler(const boost::system::error_code &err) {
    if (!err) {
        auto session = std::make_shared<Session>(*this, std::move(_acceptor_socket));
        session->start();
        _sessions.emplace(session);
        accept();
    } else {
        std::cerr << "accept error" << std::endl;
    }
}

void PacketDispatcher::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_command_endpoint,
                                       boost::bind(&PacketDispatcher::commandReadHandler, this, _1, _2));
}

void PacketDispatcher::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    enum action_type {
        EDIT_CONFIG,
    };

    static const std::map<std::string, action_type> ACTIONS = {
            {"edit_config", EDIT_CONFIG},
    };

    if(!err) {
        try {
            rapidjson::Document document;
            document.Parse(_command_buffer, bytes_transferred);
            if(!document.HasParseError()){
                if(document.HasMember("action") && document["action"].IsString()){
                    auto it = ACTIONS.find(document["action"].GetString());
                    if(it != ACTIONS.end()) {
                        switch (it->second) {
                            case EDIT_CONFIG:
                                commandEditConfig(document);
                                break;
                        }
                    }
                } else{
                    std::string response = R"({"status":"fail", "reason":"action not provided or not implemented"})";
                    _command_socket.send_to(boost::asio::buffer(response), _remote_command_endpoint);
                }
            } else {
                //std::string error_extract(&_command_buffer[document.GetErrorOffset()], std::min(32ul, bytes_transferred - document.GetErrorOffset()));
                std::string response = R"({"status":"fail", "reason":"error while parsing"})";
                _command_socket.send_to(boost::asio::buffer(response), _remote_command_endpoint);
            }
        } catch(const std::exception &e) {
            std::cout << e.what() << std::endl;
        }
        commandRead();
    } else {
        std::cerr << "command socket error !" << std::endl;
    }
}

void PacketDispatcher::commandEditConfig(const rapidjson::Document &document) {
    std::vector<std::string> changes;
    if (document.HasMember("id") && document["id"].IsUint()) {
        if (_module_id != document["id"].GetUint()) {
            _module_id = document["id"].GetUint();
            changes.emplace_back(R"("id")");
        }
        id_set = true;
    }
    if (document.HasMember("consumer_path_address") && document["consumer_path_address"].IsString() &&
            document.HasMember("consumer_path_port") && document["consumer_path_port"].IsUint()) {
        bool has_change = false;
        if (_consumer_remote_ip != document["consumer_path_address"].GetString()) {
            _consumer_remote_ip = document["consumer_path_address"].GetString();
            has_change = true;
        }
        if (_consumer_remote_port != document["consumer_path_port"].GetUint()) {
            _consumer_remote_port = document["consumer_path_port"].GetUint();
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back(R"("consumer_path")");
        }
    }
    if (document.HasMember("producer_path_address") && document["producer_path_address"].IsString() &&
        document.HasMember("producer_path_port") && document["producer_path_port"].IsUint()) {
        bool has_change = false;
        if (_producer_remote_ip != document["producer_path_address"].GetString()) {
            _producer_remote_ip = document["producer_path_address"].GetString();
            has_change = true;
        }
        if (_producer_remote_port != document["producer_path_port"].GetUint()) {
            _producer_remote_port = document["producer_path_port"].GetUint();
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back(R"("producer_path")");
        }
    }
    if (document.HasMember("id") && document["id"].IsUint()) {
        _module_id = document["id"].GetUint();
        id_set = true;
    }
    if (id_set) {
        std::stringstream ss;
        ss << R"({"id":)" << _module_id << R"(,"type":"reply","to":"edit_config","changes":[)";
        bool first = true;
        for(const auto& change : changes) {
            if (first) {
                first = false;
            } else {
                ss << ",";
            }
            ss << change;
        }
        ss << "]}";
        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
    }
}

void PacketDispatcher::commandList(const rapidjson::Document &document) {

}
