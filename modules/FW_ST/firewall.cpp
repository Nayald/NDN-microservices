#include "firewall.h"

#include <boost/bind.hpp>

#include "network/tcp_master_face.h"
#include "network/tcp_face.h"
#include "network/udp_master_face.h"
#include "network/udp_face.h"
#include "log/logger.h"

Firewall::Firewall(const std::string &name, uint16_t local_port, uint16_t local_command_port)
        : Module(1)
        , _name(name)
        , _command_socket(_ios, {{}, local_command_port})
        , _report_timer(_ios)
        , _delay_between_report(0) {
    _tcp_ingress_master_face = std::make_shared<TcpMasterFace>(_ios, 16, local_port);
    _udp_ingress_master_face = std::make_shared<UdpMasterFace>(_ios, 16, local_port);
}

void Firewall::run() {
    commandRead();
    _tcp_ingress_master_face->listen(boost::bind(&Firewall::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&Firewall::onIngressInterest, this, _1, _2),
                                     boost::bind(&Firewall::onIngressData, this, _1, _2),
                                     boost::bind(&Firewall::onMasterFaceError, this, _1, _2));
    _udp_ingress_master_face->listen(boost::bind(&Firewall::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&Firewall::onIngressInterest, this, _1, _2),
                                     boost::bind(&Firewall::onIngressData, this, _1, _2),
                                     boost::bind(&Firewall::onMasterFaceError, this, _1, _2));
}

void Firewall::onIngressInterest(const std::shared_ptr<Face> &ingress_face, const ndn::Interest &interest) {
    if (_drop_interest && _filter.get(interest.getName())) {
        ++_interest_drop_counter;
    } else {
        for (auto& egress_face : _egress_faces) {
            egress_face->send(interest);
        }
    }
}

void Firewall::onIngressData(const std::shared_ptr<Face> &ingress_face, const ndn::Data &data) {
    if (_drop_data && _filter.get(data.getName())) {
        ++_data_drop_counter;
    } else {
        for (auto& egress_face : _egress_faces) {
            egress_face->send(data);
        }
    }
}

void Firewall::onEgressInterest(const std::shared_ptr<Face> &egress_face, const ndn::Interest &interest) {
    if (_drop_interest && _filter.get(interest.getName())) {
        ++_interest_drop_counter;
    } else {
        _tcp_ingress_master_face->sendToAllFaces(interest);
        _udp_ingress_master_face->sendToAllFaces(interest);
    }
}

void Firewall::onEgressData(const std::shared_ptr<Face> &egress_face, const ndn::Data &data) {
    if (_drop_data && _filter.get(data.getName())) {
        ++_data_drop_counter;
    } else {
        _tcp_ingress_master_face->sendToAllFaces(data);
        _udp_ingress_master_face->sendToAllFaces(data);
    }
}

void Firewall::onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "new " << " face with ID = " << face->getFaceId() << " form master face with ID = " << master_face->getMasterFaceId();
    logger::log(logger::INFO, ss.str());
}

void Firewall::onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << " face with ID = " << face->getFaceId() << " from master face with ID = " << master_face->getMasterFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());
}

void Firewall::onFaceError(const std::shared_ptr<Face> &face) {
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

void Firewall::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_command_endpoint,
                                       boost::bind(&Firewall::commandReadHandler, this, _1, _2));
}

void Firewall::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    enum action_type {
        EDIT_CONFIG,
        ADD_FACE,
        DEL_FACE,
        ADD_RULES,
        DEL_RULES,
    };

    static const std::map<std::string, action_type> ACTIONS = {
            {"edit_config", EDIT_CONFIG},
            {"add_face", ADD_FACE},
            {"del_face", DEL_FACE},
            {"add_rules", ADD_RULES},
            {"del_rules", DEL_RULES},
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
                            case ADD_RULES:
                                commandAddRules(document);
                                break;
                            case DEL_RULES:
                                commandDelRules(document);
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

void Firewall::commandEditConfig(const rapidjson::Document &document) {
    std::vector<std::string> changes;
    if (document.HasMember("drop_interest") && document["drop_interest"].IsBool()) {
        bool has_change = false;
        bool drop_interest = document["drop_interest"].GetBool();
        if (drop_interest != _drop_interest) {
            _drop_interest = drop_interest;
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("drop_interest");
        }
    }

    if (document.HasMember("drop_data") && document["drop_data"].IsBool()) {
        bool has_change = false;
        bool drop_data = document["drop_data"].GetBool();
        if (drop_data != _drop_data) {
            _drop_data = drop_data;
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("drop_data");
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
                _report_timer.async_wait(boost::bind(&Firewall::commandReport, this, _1));
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

void Firewall::commandAddFace(const rapidjson::Document &document) {
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
            face->open(boost::bind(&Firewall::onEgressInterest, this, _1, _2),
                       boost::bind(&Firewall::onEgressData, this, _1, _2),
                       boost::bind(&Firewall::onFaceError, this, _1));
            std::stringstream ss;
            ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_face", "face_id":)" << face->getFaceId() << "}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
        }
    }
}

void Firewall::commandDelFace(const rapidjson::Document &document) {
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

void Firewall::commandAddRules(const rapidjson::Document &document) {
    if (document.HasMember("rules") && document["rules"].IsArray()) {
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_rules", )";
        auto&& rules = document["rules"].GetArray();
        if (rules.Empty()) {
            ss << R"("status":"fail", "reason":"empty rule list"})";
        } else {
            for (auto &rule : rules) {
                if (rule.IsArray()) {
                    auto rule_info = rule.GetArray();
                    if (rule_info.Size() == 3 && rule_info[0].IsString() && rule_info[1].IsBool() && rule_info[2].IsUint()) {
                        ndn::Name name_prefix(rule_info[0].GetString());
                        _filter.insert(name_prefix, rule_info[1].GetBool());
                        std::stringstream ss1;
                        ss1 << name_prefix << " with " << (rule_info[1].GetBool() ? "drop" : "accept") << " policy added by manager";
                        logger::log(logger::INFO, ss1.str());
                    }
                }
            }
            ss << R"("status":"success"})";
        }
        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
    }
}

void Firewall::commandDelRules(const rapidjson::Document &document) {
    if (document.HasMember("rules") && document["rules"].IsArray()) {
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"del_route", )";
        auto&& rules = document["rules"].GetArray();
        if (rules.Empty()) {
            ss << R"("status":"fail", "reason":"empty prefix list"})";
        } else {
            for (auto &rule : rules) {
                if (rule.IsString()) {
                    ndn::Name name_prefix(rule.GetString());
                    _filter.remove(name_prefix);
                    std::stringstream ss1;
                    ss1 << name_prefix << " removed by manager";
                    logger::log(logger::INFO, ss1.str());
                }
            }
            ss << R"("status":"success"})";
        }
        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
        std::cout << _filter.toJSON() << std::endl;
    }
}

void Firewall::commandList(const rapidjson::Document &document) {
    std::stringstream ss;
    ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"list")" << "}";
    _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
}

void Firewall::commandReport(const boost::system::error_code &err) {
    if (!err && _manager_endpoint.address() != boost::asio::ip::address_v4::any() && _manager_endpoint.port() != 0) {
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"report", "action":"cache_status", "interest_drop":)" << _interest_drop_counter << R"(, "data_drop":)" << _data_drop_counter << "}";
        _command_socket.send_to(boost::asio::buffer(ss.str()), _manager_endpoint);
    }
    if(_report_enable) {
        _report_timer.expires_from_now(_delay_between_report);
        _report_timer.async_wait(boost::bind(&Firewall::commandReport, this, _1));
    }
}

