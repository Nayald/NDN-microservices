#include "strategy_router.h"

#include "ndn-cxx/security/key-chain.hpp"

#include <boost/bind.hpp>

#include "network/tcp_master_face.h"
#include "network/tcp_face.h"
#include "network/udp_master_face.h"
#include "network/udp_face.h"
#include "multicast_strategy.h"
#include "failover_strategy.h"
#include "log/logger.h"
#include "loadbalancing_strategy.h"

StrategyRouter::StrategyRouter(const std::string &name, uint16_t local_port, uint16_t local_command_port)
        : Module(1)
        , _name(name)
        , _command_socket(_ios, {{}, local_command_port}){
    _tcp_ingress_master_face = std::make_shared<TcpMasterFace>(_ios, 16, local_port);
    _udp_ingress_master_face = std::make_shared<UdpMasterFace>(_ios, 16, local_port);
}

void StrategyRouter::run() {
    _strategy = std::unique_ptr<Strategy>(new MulticastStrategy());
    _strategy_name = "multicast";
    commandRead();
    _tcp_ingress_master_face->listen(boost::bind(&StrategyRouter::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&StrategyRouter::onIngressInterest, this, _1, _2),
                                     boost::bind(&StrategyRouter::onIngressData, this, _1, _2),
                                     boost::bind(&StrategyRouter::onMasterFaceError, this, _1, _2));
    _udp_ingress_master_face->listen(boost::bind(&StrategyRouter::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&StrategyRouter::onIngressInterest, this, _1, _2),
                                     boost::bind(&StrategyRouter::onIngressData, this, _1, _2),
                                     boost::bind(&StrategyRouter::onMasterFaceError, this, _1, _2));
}

void StrategyRouter::onIngressInterest(const std::shared_ptr<Face> &ingress_face, const ndn::Interest &interest) {
    for (const auto& egress_face : _strategy->selectFaces(_egress_faces)) {
        egress_face->send(interest);
    }
}

void StrategyRouter::onIngressData(const std::shared_ptr<Face> &ingress_face, const ndn::Data &data) {
    for (const auto& egress_face : _strategy->selectFaces(_egress_faces)) {
        egress_face->send(data);
    }
}

void StrategyRouter::onEgressInterest(const std::shared_ptr<Face> &egress_face, const ndn::Interest &interest) {
    _tcp_ingress_master_face->sendToAllFaces(interest);
    _udp_ingress_master_face->sendToAllFaces(interest);
}

void StrategyRouter::onEgressData(const std::shared_ptr<Face> &egress_face, const ndn::Data &data) {
    _tcp_ingress_master_face->sendToAllFaces(data);
    _udp_ingress_master_face->sendToAllFaces(data);
}

void StrategyRouter::onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "new face with ID = " << face->getFaceId() << " form master face with ID = " << master_face->getMasterFaceId();
    logger::log(logger::INFO, ss.str());
}

void StrategyRouter::onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "face with ID = " << face->getFaceId() << " from master face with ID = " << master_face->getMasterFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());
}

void StrategyRouter::onFaceError(const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "face with ID = " << face->getFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());
    for (auto& egress_face : _egress_faces) {
        if(egress_face == face) {
            std::swap(egress_face, _egress_faces.back());
            _egress_faces.pop_back();
            break;
        }
    }
}

void StrategyRouter::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_command_endpoint,
                                       boost::bind(&StrategyRouter::commandReadHandler, this, _1, _2));
}

void StrategyRouter::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    enum action_type {
        EDIT_CONFIG,
        ADD_FACE,
        DEL_FACE,
        LIST
    };

    static const std::unordered_map<std::string, action_type> ACTIONS = {
            {"edit_config", EDIT_CONFIG},
            {"add_face", ADD_FACE},
            {"del_face", DEL_FACE},
            {"list", LIST}
    };

    if(!err) {
        try {
            rapidjson::Document document;
            document.Parse(_command_buffer, bytes_transferred);
            if(!document.HasParseError()){
                if(document.HasMember("action") && document["action"].IsString() && document.HasMember("id") && document["id"].IsUint()){
                    auto it = ACTIONS.find(document["action"].GetString());
                    if(it != ACTIONS.end()) {
                        switch (it->second) {
                            case EDIT_CONFIG:
                                commandEditConfig(document);
                                break;
                            case ADD_FACE:
                                commandAddFace(document);
                                break;
                            case DEL_FACE:
                                commandDelFace(document);
                                break;
                            case LIST:
                                commandList(document);
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

void StrategyRouter::commandEditConfig(const rapidjson::Document &document) {
    std::vector<std::string> changes;
    if (document.HasMember("strategy") && document["strategy"].IsString()) {
        enum strategy_type {
            MULTICAST,
            LOADBALANCING,
            FAILOVER
        };

        static const std::unordered_map<std::string, strategy_type> STRATEGIES = {
                {"multicast", MULTICAST},
                {"loadbalancing", LOADBALANCING},
                {"failover", FAILOVER}
        };

        bool has_change = false;
        if (document["strategy"].GetString() != _strategy_name) {
            auto it = STRATEGIES.find(document["strategy"].GetString());
            if (it != STRATEGIES.end()) {
                switch (it->second) {
                    case MULTICAST:
                        _strategy = std::unique_ptr<Strategy>(new MulticastStrategy());
                        _strategy_name = "multicast";
                        break;
                    case LOADBALANCING:
                        _strategy = std::unique_ptr<Strategy>(new LoadbalancingStrategy());
                        _strategy_name = "loadbalancing";
                        break;
                    case FAILOVER:
                        _strategy = std::unique_ptr<Strategy>(new FailoverStrategy());
                        _strategy_name = "failover";
                        break;
                }
                has_change = true;
            }
            if (has_change) {
                changes.emplace_back("strategy");
            }
        }
    }

    std::stringstream ss;
    ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"edit_config", "changes":[)";
    bool first = true;
    for(const auto& change : changes) {
        if (first) {
            first = false;
        } else {
            ss << ",";
        }
        ss << '"' << change << '"';
    }
    ss << "]}";
    _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
}

void StrategyRouter::commandAddFace(const rapidjson::Document &document) {
    enum layer_type {
        TCP,
        UDP,
    };

    static const std::unordered_map<std::string, layer_type> LAYERS = {
            {"tcp", TCP},
            {"udp", UDP},
    };

    if (document.HasMember("layer") && document.HasMember("address") && document.HasMember("port")
        && document["layer"].IsString() && document["address"].IsString() && document["port"].IsUint()) {
        auto it = LAYERS.find(document["layer"].GetString());
        if (it != LAYERS.end()) {
            std::shared_ptr<Face> face;
            switch (it->second) {
                case TCP:
                    face = std::make_shared<TcpFace>(_ios, document["address"].GetString(), document["port"].GetUint());
                    break;
                case UDP:
                    face = std::make_shared<UdpFace>(_ios, document["address"].GetString(), document["port"].GetUint());
                    break;
            }
            _egress_faces.push_back(face);
            face->open(boost::bind(&StrategyRouter::onEgressInterest, this, _1, _2),
                       boost::bind(&StrategyRouter::onEgressData, this, _1, _2),
                       boost::bind(&StrategyRouter::onFaceError, this, _1));
            std::stringstream ss;
            ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_face", "face_id":)" << face->getFaceId() << "}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
        }
    }
}

void StrategyRouter::commandDelFace(const rapidjson::Document &document) {
    if (document.HasMember("face_id") && document["face_id"].IsUint()) {
        size_t face_id = document["face_id"].GetUint();
        bool ok = false;
        for (auto& egress_face : _egress_faces) {
            if (egress_face->getFaceId() == face_id) {
                egress_face->close();
                std::swap(egress_face, _egress_faces[_egress_faces.size() - 1]);
                _egress_faces.pop_back();
                ok = true;
                break;
            }
        }
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"del_face", "face_id":)" << face_id << R"(, "status":)" << ok << "}";
        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
    }
}

void StrategyRouter::commandList(const rapidjson::Document &document) {
    std::stringstream ss;
    ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"list", "strategy":")" << _strategy_name << "\"}";
    _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
}
