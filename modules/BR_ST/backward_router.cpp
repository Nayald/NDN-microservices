#include "backward_router.h"

#include <boost/bind.hpp>

#include "network/tcp_master_face.h"
#include "network/udp_master_face.h"
#include "network/tcp_face.h"
#include "network/udp_face.h"
#include "log/logger.h"

BackwardRouter::BackwardRouter(const std::string &name, size_t max_size, uint16_t local_port, uint16_t local_command_port)
        : Module(1)
        , _name(name)
        , _pit(max_size)
        , _command_socket(_ios, {{}, local_command_port}) {
    _tcp_ingress_master_face = std::make_shared<TcpMasterFace>(_ios, 16, local_port);
    _udp_ingress_master_face = std::make_shared<UdpMasterFace>(_ios, 16, local_port);
}

void BackwardRouter::run() {
    commandRead();
    _tcp_ingress_master_face->listen(boost::bind(&BackwardRouter::onMasterFaceNotification, this, _1, _2),
                               boost::bind(&BackwardRouter::onIngressInterest, this, _1, _2),
                               boost::bind(&BackwardRouter::onIngressData, this, _1, _2),
                               boost::bind(&BackwardRouter::onMasterFaceError, this, _1, _2));
    _udp_ingress_master_face->listen(boost::bind(&BackwardRouter::onMasterFaceNotification, this, _1, _2),
                               boost::bind(&BackwardRouter::onIngressInterest, this, _1, _2),
                               boost::bind(&BackwardRouter::onIngressData, this, _1, _2),
                               boost::bind(&BackwardRouter::onMasterFaceError, this, _1, _2));
}

void BackwardRouter::onIngressInterest(const std::shared_ptr<Face> &ingress_face, const ndn::Interest &interest) {
    if (_pit.insert(interest, ingress_face)) {
        for (const auto& egress_face : _egress_faces) {
            egress_face->send(interest);
        }
    }
}

void BackwardRouter::onIngressData(const std::shared_ptr<Face> &ingress_face, const ndn::Data &data) {
    // drop
}

void BackwardRouter::onEgressInterest(const std::shared_ptr<Face> &egress_face, const ndn::Interest &interest) {
    // drop
}

void BackwardRouter::onEgressData(const std::shared_ptr<Face> &egress_face, const ndn::Data &data) {
    auto&& ingress_faces = _pit.get(data);
    for (const auto& ingress_face : ingress_faces) {
        ingress_face->send(data);
    }
}

void BackwardRouter::onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "new " << " face with ID = " << face->getFaceId() << " from master face with ID = " << master_face->getMasterFaceId();
    logger::log(logger::INFO, ss.str());
}

void BackwardRouter::onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "face with ID = " << face->getFaceId() << " from master face with ID = " << master_face->getMasterFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());

}

void BackwardRouter::onFaceError(const std::shared_ptr<Face> &face) {
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

void BackwardRouter::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_command_endpoint,
                                       boost::bind(&BackwardRouter::commandReadHandler, this, _1, _2));
}

void BackwardRouter::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    enum action_type {
        EDIT_CONFIG,
        ADD_FACE,
        DEL_FACE,
    };

    static const std::unordered_map<std::string, action_type> ACTIONS = {
            {"edit_config", EDIT_CONFIG},
            {"add_face", ADD_FACE},
            {"del_face", DEL_FACE},
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
                        }
                    }
                } else{
                    //std::string response = R"({"status":"fail", "reason":"action not provided or not implemented"})";
                    //_command_socket.send_to(boost::asio::buffer(response), _remote_command_endpoint);
                }
            } else {
                //std::string error_extract(&_command_buffer[document.GetErrorOffset()], std::min(32ul, bytes_transferred - document.GetErrorOffset()));
                //std::string response = R"({"status":"fail", "reason":"error while parsing"})";
                //_command_socket.send_to(boost::asio::buffer(response), _remote_command_endpoint);
            }
        } catch(const std::exception &e) {
            std::cout << e.what() << std::endl;
        }
        commandRead();
    } else {
        std::cerr << "command socket error !" << std::endl;
    }
}

void BackwardRouter::commandEditConfig(const rapidjson::Document &document) {
    std::vector<std::string> changes;
    if (document.HasMember("size") && document["size"].IsUint()) {
        bool has_change = false;
        size_t new_size = document["size"].GetUint();
        if (new_size != _pit.getSize()) {
            _pit.setSize(new_size);
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("size");
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

void BackwardRouter::commandAddFace(const rapidjson::Document &document) {
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
            face->open(boost::bind(&BackwardRouter::onEgressInterest, this, _1, _2),
                       boost::bind(&BackwardRouter::onEgressData, this, _1, _2),
                       boost::bind(&BackwardRouter::onFaceError, this, _1));
            std::stringstream ss;
            ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_face", "face_id":)" << face->getFaceId() << "}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
        }
    }
}

void BackwardRouter::commandDelFace(const rapidjson::Document &document) {
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

void BackwardRouter::commandList(const rapidjson::Document &document) {

}
