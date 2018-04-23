#include "content_store.h"

#include <boost/bind.hpp>

#include "network/tcp_master_face.h"
#include "network/tcp_face.h"
#include "network/udp_master_face.h"
#include "network/udp_face.h"
#include "log/logger.h"

ContentStore::ContentStore(const std::string &name, size_t size, uint16_t local_port, uint16_t local_command_port)
        : Module(1)
        , _name(name)
        , _cs(size)
        , _command_socket(_ios, {{}, local_command_port})
        , _report_timer(_ios)
        , _delay_between_report(0) {
    _tcp_ingress_master_face = std::make_shared<TcpMasterFace>(_ios, 16, local_port);
    _udp_ingress_master_face = std::make_shared<UdpMasterFace>(_ios, 16, local_port);
}

void ContentStore::run() {
    commandRead();
    _tcp_ingress_master_face->listen(boost::bind(&ContentStore::onMasterFaceNotification, this, _1, _2),
                               boost::bind(&ContentStore::onIngressInterest, this, _1, _2),
                               boost::bind(&ContentStore::onIngressData, this, _1, _2),
                               boost::bind(&ContentStore::onMasterFaceError, this, _1, _2));
    _udp_ingress_master_face->listen(boost::bind(&ContentStore::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&ContentStore::onIngressInterest, this, _1, _2),
                                     boost::bind(&ContentStore::onIngressData, this, _1, _2),
                                     boost::bind(&ContentStore::onMasterFaceError, this, _1, _2));
}

void ContentStore::onIngressInterest(const std::shared_ptr<Face> &ingress_face, const ndn::Interest &interest) {
    //std::cout << interest.getName();
    auto entry = _cs.get(interest);
    if (entry) {
        //std::cout << " -> respond with cache" << std::endl;
        ingress_face->send(entry->getData());
        ++_hit_counter;
    } else {
        //std::cout << " -> forward packet" << std::endl;
        for (auto& egress_face : _egress_faces) {
            egress_face->send(interest);
        }
        ++_miss_counter;
    }
}

void ContentStore::onIngressData(const std::shared_ptr<Face> &ingress_face, const ndn::Data &data) {
    _cs.insert(data);
    for (auto& egress_face : _egress_faces) {
        egress_face->send(data);
    }
}

void ContentStore::onEgressInterest(const std::shared_ptr<Face> &egress_face, const ndn::Interest &interest) {
    //std::cout << interest.getName();
    auto entry = _cs.get(interest);
    if (entry) {
        //std::cout << " -> respond with cache" << std::endl;
        egress_face->send(entry->getData());
        ++_hit_counter;
    } else {
        //std::cout << " -> forward packet" << std::endl;
        _tcp_ingress_master_face->sendToAllFaces(interest);
        _udp_ingress_master_face->sendToAllFaces(interest);
        ++_miss_counter;
    }
}

void ContentStore::onEgressData(const std::shared_ptr<Face> &egress_face, const ndn::Data &data) {
    _cs.insert(data);
    _tcp_ingress_master_face->sendToAllFaces(data);
    _udp_ingress_master_face->sendToAllFaces(data);
}

void ContentStore::onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "new " << " face with ID = " << face->getFaceId() << " form master face with ID = " << master_face->getMasterFaceId();
    logger::log(logger::INFO, ss.str());
}

void ContentStore::onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << " face with ID = " << face->getFaceId() << " from master face with ID = " << master_face->getMasterFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());
}

void ContentStore::onFaceError(const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << " face with ID = " << face->getFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());
    for (auto& egress_face : _egress_faces) {
        if(egress_face == face) {
            std::swap(egress_face, _egress_faces.back());
            _egress_faces.pop_back();
            break;
        }
    }
}

void ContentStore::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_command_endpoint,
                                       boost::bind(&ContentStore::commandReadHandler, this, _1, _2));
}

void ContentStore::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    enum action_type {
        EDIT_CONFIG,
        ADD_FACE,
        DEL_FACE,
    };

    static const std::map<std::string, action_type> ACTIONS = {
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

void ContentStore::commandEditConfig(const rapidjson::Document &document) {
    std::vector<std::string> changes;
    if (document.HasMember("size") && document["size"].IsUint()) {
        bool has_change = false;
        size_t new_size = document["size"].GetUint();
        if (new_size != _cs.getSize()) {
            _cs.setSize(new_size);
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("size");
        }
    }
    if (document.HasMember("manager_address") && document.HasMember("manager_port") && document["manager_address"].IsString() && document["manager_port"].IsUint()) {
        bool has_change = false;
        boost::asio::ip::udp::endpoint new_endpoint(boost::asio::ip::address::from_string(document["manager_address"].GetString()), document["manager_port"].GetUint());
        if (new_endpoint != _manager_endpoint) {
            _manager_endpoint = new_endpoint;
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("manager_endpoint");
        }
    }

    if (document.HasMember("report_each") && document["report_each"].IsUint()) {
        bool has_change = false;
        boost::posix_time::milliseconds delay_between_report(document["report_each"].GetUint());
        if (delay_between_report != _delay_between_report) {
            _delay_between_report = delay_between_report;
            has_change = true;
        }
        if (_delay_between_report.total_milliseconds() > 0) {
            if (!_report_enable) {
                _report_enable = true;
                _report_timer.expires_from_now(_delay_between_report);
                _report_timer.async_wait(boost::bind(&ContentStore::commandReport, this, _1));
            }
        } else {
            _report_enable = false;
        }
        if (has_change) {
            changes.emplace_back("report_each");
        }
    }

    std::stringstream ss;
    ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"edit_config", "changes":[)";
    bool first = true;
    for(const auto& change : changes) {
        if (first) {
            first = false;
        } else {
            ss << ", ";
        }
        ss << '"' << change << '"';
    }
    ss << "]}";
    _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
}

void ContentStore::commandAddFace(const rapidjson::Document &document) {
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
            face->open(boost::bind(&ContentStore::onEgressInterest, this, _1, _2),
                       boost::bind(&ContentStore::onEgressData, this, _1, _2),
                       boost::bind(&ContentStore::onFaceError, this, _1));
            std::stringstream ss;
            ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_face", "face_id":)" << face->getFaceId() << "}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
        }
    }
}

void ContentStore::commandDelFace(const rapidjson::Document &document) {
    if (document.HasMember("face_id") && document["face_id"].IsUint()) {
        size_t face_id = document["face_id"].GetUint();
        bool ok = false;
        for (auto& egress_face : _egress_faces) {
            if (egress_face->getFaceId() == face_id) {
                egress_face->close();
                std::swap(egress_face, _egress_faces.back());
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

void ContentStore::commandList(const rapidjson::Document &document) {
    std::stringstream ss;
    ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"list", "size":)" << _cs.getSize() << R"(})";
    _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
}

void ContentStore::commandReport(const boost::system::error_code &err) {
    if (!err && _manager_endpoint.address() != boost::asio::ip::address_v4::any() && _manager_endpoint.port() != 0) {
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"report", "action":"cache_status", "hit_count":)" << _hit_counter << R"(, "miss_count":)" << _miss_counter << "}";
        _command_socket.send_to(boost::asio::buffer(ss.str()), _manager_endpoint);
    }
    if(_report_enable) {
        _report_timer.expires_from_now(_delay_between_report);
        _report_timer.async_wait(boost::bind(&ContentStore::commandReport, this, _1));
    }
}

