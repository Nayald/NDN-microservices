#include "signature_verifier.h"

#include <boost/bind.hpp>

#include <unordered_map>

#include "network/tcp_master_face.h"
#include "network/tcp_face.h"
#include "network/udp_master_face.h"
#include "network/udp_face.h"
#include "log/logger.h"

//static BIO *bio = BIO_new_mem_buf(RSA_PUBLIC_KEY.c_str(), RSA_PUBLIC_KEY.length());
//static BIO *bio = BIO_new_mem_buf(DEFAULT_RSA_PUBLIC_KEY_DER, sizeof(DEFAULT_RSA_PUBLIC_KEY_DER));
//static EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
//static EVP_PKEY* pkey = d2i_PUBKEY_bio(bio, NULL);
//static EVP_PKEY* pkey = d2i_PUBKEY_fp(fopen("tan.pub", "r"), NULL);

SignatureVerifier::SignatureVerifier(const std::string &name, uint16_t local_port, uint16_t command_local_port)
        : Module(1)
        , _name(name)
        , _command_socket(_ios, {{}, 10000})
        , _report_timer(_ios)
        , _delay_between_report(0) {
    _tcp_ingress_master_face = std::make_shared<TcpMasterFace>(_ios, 16, local_port);
    _udp_ingress_master_face = std::make_shared<UdpMasterFace>(_ios, 16, local_port);
}

void SignatureVerifier::run() {
    commandRead();
    _tcp_ingress_master_face->listen(boost::bind(&SignatureVerifier::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&SignatureVerifier::onIngressInterest, this, _1, _2),
                                     boost::bind(&SignatureVerifier::onIngressData, this, _1, _2),
                                     boost::bind(&SignatureVerifier::onMasterFaceError, this, _1, _2));
    _udp_ingress_master_face->listen(boost::bind(&SignatureVerifier::onMasterFaceNotification, this, _1, _2),
                                     boost::bind(&SignatureVerifier::onIngressInterest, this, _1, _2),
                                     boost::bind(&SignatureVerifier::onIngressData, this, _1, _2),
                                     boost::bind(&SignatureVerifier::onMasterFaceError, this, _1, _2));
}

void SignatureVerifier::onIngressInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest) {
    for (const auto& egress_face : _egress_faces) {
        egress_face->send(interest);
    }
}

void SignatureVerifier::onIngressData(const std::shared_ptr<Face> &face, const ndn::Data &data) {
    if (data.getSignature().getType() != ndn::tlv::SignatureTypeValue::DigestSha256) {
        auto it = _pkeys.find(data.getSignature().getKeyLocator().getName());
        if (it != _pkeys.end()) {
            int result = verify(data.wireEncode().value(),
                                data.wireEncode().value_size() - data.getSignature().getValue().size(),
                                data.getSignature().getValue().value(), data.getSignature().getValue().value_size(),
                                it->second);
            if (result > 0) {
                if (_report_enable) {
                    _invalid_signature_packet_names.emplace(data.getName().toUri());
                }
                if (!_drop) {
                    for (const auto &egress_face : _egress_faces) {
                        egress_face->send(data);
                    }
                }
            } else {
                for (const auto &egress_face : _egress_faces) {
                    egress_face->send(data);
                }
            }
        } else if (!_no_key_drop) {
            for (const auto &egress_face : _egress_faces) {
                egress_face->send(data);
            }
        }
    } else if (!_unsigned_drop) {
        for (const auto &egress_face : _egress_faces) {
            egress_face->send(data);
        }
    }
}

void SignatureVerifier::onEgressInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest) {
    _tcp_ingress_master_face->sendToAllFaces(interest);
    _udp_ingress_master_face->sendToAllFaces(interest);
}

void SignatureVerifier::onEgressData(const std::shared_ptr<Face> &face, const ndn::Data &data) {
    if (data.getSignature().getType() != ndn::tlv::SignatureTypeValue::DigestSha256) {
        auto it = _pkeys.find(data.getSignature().getKeyLocator().getName());
        if (it != _pkeys.end()) {
            int result = verify(data.wireEncode().value(),
                                data.wireEncode().value_size() - data.getSignature().getValue().size(),
                                data.getSignature().getValue().value(), data.getSignature().getValue().value_size(),
                                it->second);
            if (result > 0) {
                if (_report_enable) {
                    _invalid_signature_packet_names.emplace(data.getName().toUri());
                }
                if (!_drop) {
                    _tcp_ingress_master_face->sendToAllFaces(data);
                    _udp_ingress_master_face->sendToAllFaces(data);
                }
            } else {
                _tcp_ingress_master_face->sendToAllFaces(data);
                _udp_ingress_master_face->sendToAllFaces(data);
            }
        } else if (!_no_key_drop) {
            _tcp_ingress_master_face->sendToAllFaces(data);
            _udp_ingress_master_face->sendToAllFaces(data);
        }
    } else if (!_unsigned_drop) {
        _tcp_ingress_master_face->sendToAllFaces(data);
        _udp_ingress_master_face->sendToAllFaces(data);
    }
}

void SignatureVerifier::onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << "new " << face->getUnderlyingProtocol() << " face with ID = " << face->getFaceId() << " form "
       << master_face->getUnderlyingProtocol() << " master face with ID = " << master_face->getMasterFaceId();
    logger::log(logger::INFO, ss.str());
}

void SignatureVerifier::onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << face->getUnderlyingProtocol() << " face with ID = " << face->getFaceId() << " from "
       << master_face->getUnderlyingProtocol() << " master face with ID = " << master_face->getMasterFaceId()
       << " can't process normally";
    logger::log(logger::ERROR, ss.str());
}

void SignatureVerifier::onFaceError(const std::shared_ptr<Face> &face) {
    std::stringstream ss;
    ss << face->getUnderlyingProtocol() << " face with ID = " << face->getFaceId() << " can't process normally";
    logger::log(logger::ERROR, ss.str());
    for (auto& egress_face : _egress_faces) {
        if(egress_face == face) {
            std::swap(egress_face, _egress_faces.back());
            _egress_faces.pop_back();
            break;
        }
    }
}

void SignatureVerifier::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_command_endpoint,
                                       boost::bind(&SignatureVerifier::commandReadHandler, this, _1, _2));
}

void SignatureVerifier::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    enum action_type {
        EDIT_CONFIG,
        ADD_FACE,
        DEL_FACE,
        ADD_KEYS,
        DEL_KEYS,
        LIST
    };

    static const std::unordered_map<std::string, action_type> ACTIONS = {
            {"edit_config", EDIT_CONFIG},
            {"add_face", ADD_FACE},
            {"del_face", DEL_FACE},
            {"add_keys", ADD_KEYS},
            {"del_keys", DEL_KEYS},
            {"list", LIST},
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
                            case ADD_KEYS:
                                commandAddKeys(document);
                                break;
                            case DEL_KEYS:
                                commandDelKeys(document);
                                break;
                            case LIST:
                                commandList(document);
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

void SignatureVerifier::commandEditConfig(const rapidjson::Document &document) {
    std::vector<std::string> changes;
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
                _report_timer.async_wait(boost::bind(&SignatureVerifier::commandReport, this, _1));
            }
        } else {
            _report_enable = false;
        }
        if (has_change) {
            changes.emplace_back("report_each");
        }
    }
    if (document.HasMember("drop") && document["drop"].IsBool()) {
        bool has_change = false;
        bool drop = document["drop"].GetBool();
        if (_drop != drop) {
            _drop = drop;
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("drop");
        }
    }
    if (document.HasMember("no_key_drop") && document["no_key_drop"].IsBool()) {
        bool has_change = false;
        bool no_key_drop = document["no_key_drop"].GetBool();
        if (_no_key_drop != no_key_drop) {
            _no_key_drop = no_key_drop;
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("no_key_drop");
        }
    }
    if (document.HasMember("unsigned_drop") && document["unsigned_drop"].IsBool()) {
        bool has_change = false;
        bool unsigned_drop = document["unsigned_drop"].GetBool();
        if (_unsigned_drop != unsigned_drop) {
            _unsigned_drop = unsigned_drop;
            has_change = true;
        }
        if (has_change) {
            changes.emplace_back("unsigned_drop");
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

void SignatureVerifier::commandAddFace(const rapidjson::Document &document) {
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
            face->open(boost::bind(&SignatureVerifier::onEgressInterest, this, _1, _2),
                       boost::bind(&SignatureVerifier::onEgressData, this, _1, _2),
                       boost::bind(&SignatureVerifier::onFaceError, this, _1));
            _egress_faces.push_back(face);
            std::stringstream ss;
            ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_face", "face_id":)" << face->getFaceId() << "}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
        }
    }
}

void SignatureVerifier::commandDelFace(const rapidjson::Document &document) {
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

void SignatureVerifier::commandAddKeys(const rapidjson::Document &document) {
    if (document.HasMember("keys") && document["keys"].IsArray()) {
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"add_keys", )";
        auto &&keys = document["keys"].GetArray();
        if (keys.Empty()) {
            ss << R"("status":"fail", "reason":"empty prefix list"})";
        } else {
            std::vector<std::string> status;
            for (auto &key : keys) {
                if (key.IsArray()) {
                    auto key_info = key.GetArray();
                    if (key_info.Size() == 3 && key_info[0].IsString() && key_info[1].IsString() && key_info[2].IsString()) {
                        ndn::Name key_name(key_info[0].GetString());
                        auto it = _pkeys.find(key_name);
                        if (it == _pkeys.end()) {
                            std::stringstream ss1;
                            std::string blob = key_info[2].GetString();
                            EVP_PKEY *pkey = PEM_read_bio_PUBKEY(BIO_new_mem_buf(blob.c_str(), blob.size()), NULL, NULL, NULL);
                            if (pkey != nullptr) {
                                _pkeys.emplace(key_name, pkey);
                                ss1 << "key " << key_name << " added by manager";
                                status.emplace_back("success");
                            } else {
                                ss1 << "error while adding key " << key_name;
                                status.emplace_back("fail");
                            }
                            logger::log(logger::INFO, ss1.str());
                        }
                    }
                }
            }
            ss << R"("status":[)";
            bool first = true;
            for(const auto& s : status) {
                if (first) {
                    first = false;
                } else {
                    ss << ",";
                }
                ss << '"' << s << '"';
            }
            ss << "]}";
        }
        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
    }
}

void SignatureVerifier::commandDelKeys(const rapidjson::Document &document) {
    if (document.HasMember("keys") && document["keys"].IsArray()) {
        std::stringstream ss;
        ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint() << R"(, "action":"del_keys", )";
        auto &&keys = document["keys"].GetArray();
        if (keys.Empty()) {
            ss << R"("status":"fail", "reason":"empty prefix list"})";
        } else {
            for (auto &key : keys) {
                if (key.IsString()) {
                    ndn::Name key_name(key.GetString());
                    auto it = _pkeys.find(key_name);
                    if (it != _pkeys.end()) {
                        EVP_PKEY *pkey = it->second;
                        _pkeys.erase(key_name);
                        EVP_PKEY_free(pkey);
                        std::stringstream ss1;
                        ss1 << "key with name " << key_name << " removed by manager";
                        logger::log(logger::INFO, ss1.str());
                    }
                }
                ss << R"("status":"success"})";
            }
        }
        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
    }
}

void SignatureVerifier::commandList(const rapidjson::Document &document) {
    std::stringstream ss;
    ss << R"({"name":")" << _name << R"(", "type":"reply", "id":)" << document["id"].GetUint()
       << R"(, "action":"list", "manager_address":")" << _manager_endpoint.address() << R"(", "manager_port":)" << _manager_endpoint.port()
       << R"(, "drop":)" << _drop << R"(, "no_key_drop":)" << _no_key_drop << R"(, "unsigned_drop":)" << _unsigned_drop << "}";
    _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_command_endpoint);
}

void SignatureVerifier::commandReport(const boost::system::error_code &err) {
    if (!err) {
        std::set<std::string> current;
        std::swap(current, _invalid_signature_packet_names);
        if (!current.empty() && _manager_endpoint.address() != boost::asio::ip::address_v4::any() && _manager_endpoint.port() != 0) {
            std::stringstream ss;
            ss << R"({"type":"report", "name":")" << _name << R"(", "action":"invalid_signature", "invalid_signature_names":[)";
            size_t rank = 0;
            for (const auto &name : current) {
                if (name.length() + ss.tellp() + 6 < 65500) {
                    if (rank) {
                        ss << ", ";
                    }
                    ss << "\"" << name << "\"";
                    ++rank;
                } else {
                    ss << "]}";
                    _command_socket.send_to(boost::asio::buffer(ss.str()), _manager_endpoint);
                    ss.str("");
                    ss << R"({"type":"report", "name":")" << _name << R"(", "action":"invalid_signature", "invalid_signature_packet_names":[)";
                    rank = 0;
                }
            }
            ss << "]}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _manager_endpoint);
        }
        if (_report_enable) {
            _report_timer.expires_from_now(_delay_between_report);
            _report_timer.async_wait(boost::bind(&SignatureVerifier::commandReport, this, _1));
        }
    }
}

// from openssl example
int SignatureVerifier::verify(const unsigned char* msg, size_t mlen, const unsigned char* sig, size_t slen, EVP_PKEY* pkey) {
    /* Returned to caller */
    int result = -1;
    if (!msg || !mlen || !sig || !slen || !pkey) {
        //printf("a least one of the parameters is undefined\n");
        return result;
    }
    EVP_MD_CTX *ctx = NULL;
    do {
        ctx = EVP_MD_CTX_create();
        if (ctx == NULL) {
            break; /* failed */
        }
        int rc = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        if (rc != 1) {
            break; /* failed */
        }
        rc = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey);
        if (rc != 1) {
            break; /* failed */
        }
        rc = EVP_DigestVerifyUpdate(ctx, msg, mlen);
        if (rc != 1) {
            break; /* failed */
        }
        /* Clear any errors for the call below */
        ERR_clear_error();
        rc = EVP_DigestVerifyFinal(ctx, sig, slen);
        if (rc != 1) {
            break; /* failed */
        }
        result = 0;
    } while (0);
    if (ctx) {
        EVP_MD_CTX_destroy(ctx);
        ctx = NULL;
    }
    return !!result;
}
