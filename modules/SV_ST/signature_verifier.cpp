#include "signature_verifier.h"

#include <boost/bind.hpp>

#include "rapidjson/document.h"

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

static int verify(const unsigned char* msg, size_t mlen, const unsigned char* sig, size_t slen, EVP_PKEY* pkey);

SignatureVerifier::SignatureVerifier(boost::asio::io_service &ios, uint16_t local_port, const std::string &remote_address, uint16_t remote_port)
        : _ios(ios)
        , _output_face(std::make_shared<UdpFace>(ios, remote_address, remote_port))
        , _input_master_face(std::make_shared<TcpMasterFace>(ios, 1, local_port))
        , _command_socket(ios, {boost::asio::ip::udp::v4(), 10000})
        , _report_timer(ios)
        , _delay_between_report(0) {

}

SignatureVerifier::~SignatureVerifier() = default;

void SignatureVerifier::start() {
    refreshPKeys();
    commandRead();
    _output_face->open(boost::bind(&SignatureVerifier::onOutputInterest, this, _1, _2),
                       boost::bind(&SignatureVerifier::onOutputData, this, _1, _2),
                       boost::bind(&SignatureVerifier::onFaceError, this, _1));
    _input_master_face->listen(boost::bind(&SignatureVerifier::onMasterFaceNotification, this, _1, _2),
                               boost::bind(&SignatureVerifier::onInputInterest, this, _1, _2),
                               boost::bind(&SignatureVerifier::onInputData, this, _1, _2),
                               boost::bind(&SignatureVerifier::onMasterFaceError, this, _1, _2));
}

void SignatureVerifier::onInputInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest) {
    //std::cout << interest << std::endl;
    _output_face->send(interest);
}

void SignatureVerifier::onInputData(const std::shared_ptr<Face> &face, const ndn::Data &data) {
    //std::cout << data << std::endl;
    auto it = _pkeys.find(data.getSignature().getKeyLocator().getName());
    if (it != _pkeys.end()) {
        int result = verify(data.wireEncode().value(),
                            data.wireEncode().value_size() - data.getSignature().getValue().size(),
                            data.getSignature().getValue().value(), data.getSignature().getValue().value_size(),
                            it->second.get());
        if (result > 0) {
            if(_report_enable) {
                _invalid_signature_packet_names.emplace(data.getName().toUri());
            }
            if (!_drop) {
                _output_face->send(data);
            }
            std::cout << data.getName() << " -> invalid signature" << std::endl;
        } else {
            _output_face->send(data);
            std::cout << data.getName() << " -> valid signature" << std::endl;
        }
    } else if (!_no_key_drop) {
        _output_face->send(data);
        std::cout << "no key found for " << data.getSignature().getKeyLocator().getName() << std::endl;
    }
}

void SignatureVerifier::onOutputInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest) {
    _input_master_face->sendToAllFaces(interest);
}

void SignatureVerifier::onOutputData(const std::shared_ptr<Face> &face, const ndn::Data &data) {
    //std::cout << data << std::endl;
    auto it = _pkeys.find(data.getSignature().getKeyLocator().getName());
    if (it != _pkeys.end()) {
        int result = verify(data.wireEncode().value(),
                            data.wireEncode().value_size() - data.getSignature().getValue().size(),
                            data.getSignature().getValue().value(), data.getSignature().getValue().value_size(),
                            it->second.get());
        if (result > 0) {
            if(_report_enable) {
                _invalid_signature_packet_names.emplace(data.getName().toUri());
            }
            if (!_drop) {
                _input_master_face->sendToAllFaces(data);
            }
            std::cout << data.getName() << " -> invalid signature" << std::endl;
        } else {
            _input_master_face->sendToAllFaces(data);
            std::cout << data.getName() << " -> valid signature" << std::endl;
        }
    } else if (!_no_key_drop) {
        _input_master_face->sendToAllFaces(data);
	    std::cout << "no key found for " << data.getSignature().getKeyLocator().getName() << std::endl;
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
    if(face == _output_face) {
        exit(-1);
    }
}

void SignatureVerifier::commandRead() {
    _command_socket.async_receive_from(boost::asio::buffer(_command_buffer, 65536), _remote_endpoint,
                                       boost::bind(&SignatureVerifier::commandReadHandler, this, _1, _2));
}

void SignatureVerifier::commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred) {
    if (!err) {
        try {
            rapidjson::Document document;
            document.Parse(_command_buffer, bytes_transferred);
            if (!document.HasParseError()) {
                if (document.HasMember("action") && document["action"].IsString()) {
                    if (std::strcmp(document["action"].GetString(), "edit") == 0) {
                        if (document.HasMember("drop") && document["drop"].IsBool()) {
                            _drop = document["drop"].GetBool();
                        }
                        if (document.HasMember("no_key_drop") && document["no_key_drop"].IsBool()) {
                            _no_key_drop = document["no_key_drop"].GetBool();
                        }
                        if (document.HasMember("name") && document["name"].IsString()) {
                            _report_name = document["name"].GetString();
                        }
                        if (document.HasMember("address") && document["address"].IsString()
                            && document.HasMember("port") && document["port"].IsUint()) {
                            _report_endpoint = {boost::asio::ip::address::from_string(document["address"].GetString()),
                                                (unsigned short)document["port"].GetUint()};
                        }
                        if (document.HasMember("report_each") && document["report_each"].IsUint()) {
                            _delay_between_report = boost::posix_time::milliseconds(document["report_each"].GetUint());
                            if (!_report_enable && _delay_between_report.total_milliseconds() > 0) {
                                _report_enable = true;
                                _report_timer.expires_from_now(_delay_between_report);
                                _report_timer.async_wait(boost::bind(&SignatureVerifier::commandReport, this, _1));
                            } else {
                                _report_enable = false;
                            }
                        }
                    } else if (std::strcmp(document["action"].GetString(), "list") == 0) {
                        std::stringstream ss;
                        ss << R"({"name":")" << _report_name << "\""
                           << R"(, "drop":)" << (_drop ? "true" : "false")
                           << R"(, "no_key_drop":)" << (_no_key_drop ? "true" : "false")
                           << R"(, "address":")" << _report_endpoint.address() << R"(", "port":)" << _report_endpoint.port()
                           << R"(, "report_each":)" << _delay_between_report.total_milliseconds() << "}";
                        _command_socket.send_to(boost::asio::buffer(ss.str()), _remote_endpoint);
                    } else if (std::strcmp(document["action"].GetString(), "refresh") == 0) {
                        refreshPKeys();
                    } else {
                        std::string response = R"({"status":"fail", "reason":"unknown action"})";
                        _command_socket.send_to(boost::asio::buffer(response), _remote_endpoint);
                    }
                } else {
                    std::string response = R"({"status":"fail", "reason":"action not provided or not a string type"})";
                    _command_socket.send_to(boost::asio::buffer(response), _remote_endpoint);
                }
            } else {
                //std::string error_extract(&_command_buffer[document.GetErrorOffset()], std::min(32ul, bytes_transferred - document.GetErrorOffset()));
                std::string response = R"({"status":"fail", "reason":"error while parsing"})";
                _command_socket.send_to(boost::asio::buffer(response), _remote_endpoint);
            }
        } catch (const std::exception &e) {
            std::cout << e.what() << std::endl;
        }
        commandRead();
    } else {
        std::cerr << "command socket error" << std::endl;
    }
}

void SignatureVerifier::commandReport(const boost::system::error_code &err) {
    if(!err) {
        std::set<std::string> current;
        std::swap(current, _invalid_signature_packet_names);
        if(!current.empty() && _report_endpoint.address() != boost::asio::ip::address_v4::any() && _report_endpoint.port() != 0) {
            /*std::stringstream ss;
            ss << R"({"type":"report", "name":")" << _report_name << R"(", "invalid_signature_packet_names":[)";
            size_t rank = 0;
            for(const auto& name : current) {
                if(name.length() + ss.tellp() + 6 < 65500) {
                    if(rank) {
                        ss << ", ";
                    }
                    ss << "\"" << name << "\"";
                    ++rank;
                } else {
                    ss << "]}";
                    _command_socket.send_to(boost::asio::buffer(ss.str()), _report_endpoint);
                    ss.str("");
                    ss << R"({"type":"report", "name":")" << _report_name << R"(", "invalid_signature_packet_names":[)";
                    rank = 0;
                }
            }
            ss << "]}";
            _command_socket.send_to(boost::asio::buffer(ss.str()), _report_endpoint);
            */
            std::string s = R"({"type":"report", "name":")" + _report_name + R"(", "invalid_signature_packet_name":)";
            for(const auto& name : current) {
                _command_socket.send_to(boost::asio::buffer(s + "\"" + name + "\"}"), _report_endpoint);
            }
        }
        if(_report_enable) {
            _report_timer.expires_from_now(_delay_between_report);
            _report_timer.async_wait(boost::bind(&SignatureVerifier::commandReport, this, _1));
        }
    }
}

void SignatureVerifier::refreshPKeys() {
    enum KEY_TYPE {
        DER,
        PEM,
    };

    static const std::map<std::string, KEY_TYPE > key_types = {
            {"DER", DER},
            {"PEM", PEM},
    };

    _pkeys.clear();
    std::ifstream index("index.txt");
    if(index.is_open()) {
        std::string line;
        while(std::getline(index, line)) {
            size_t delem = 0, last_delem = 0;
            if ((delem = line.find(' ')) != std::string::npos) {
                ndn::Name name(line.substr(0, delem));
                last_delem = delem + 1;
                if ((delem = line.find(' ', last_delem)) != std::string::npos) {
                    auto it = key_types.find(line.substr(last_delem, delem - last_delem));
                    if (it == key_types.end()) {
                        continue;
                    }
                    std::unique_ptr<EVP_PKEY> key;
                    switch (it->second) {
                        case PEM:
                            key = std::unique_ptr<EVP_PKEY>(PEM_read_PUBKEY(fopen(line.substr(delem + 1).c_str(), "r"), NULL, NULL, NULL));
                            break;
                        case DER:
                            key = std::unique_ptr<EVP_PKEY>(d2i_PUBKEY_fp(fopen(line.substr(delem + 1).c_str(), "r"), NULL));
                            break;
                        default:
                            break;
                    }
                    if (key != nullptr) {
                        _pkeys.emplace(name, std::move(key));
                    }
                }
            }
        }
        std::stringstream ss;
        ss << _pkeys.size() << " keys have been loaded";
        logger::log(logger::INFO, ss.str());
    } else {
        std::stringstream ss;
        ss << " no index, can't load public keys";
        logger::log(logger::ERROR, ss.str());
    }
}

static int verify(const unsigned char* msg, size_t mlen, const unsigned char* sig, size_t slen, EVP_PKEY* pkey) {
    /* Returned to caller */
    int result = -1;

    if(!msg || !mlen || !sig || !slen || !pkey) {
        printf("a least one of the parameters is undefined\n");
        return result;
    }

    EVP_MD_CTX* ctx = NULL;

    do {
        ctx = EVP_MD_CTX_create();
        if(ctx == NULL) {
            break; /* failed */
        }

        int rc = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        if(rc != 1) {
            break; /* failed */
        }

        rc = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey);
        if(rc != 1) {
            break; /* failed */
        }

        rc = EVP_DigestVerifyUpdate(ctx, msg, mlen);
        if(rc != 1) {
            break; /* failed */
        }

        /* Clear any errors for the call below */
        ERR_clear_error();

        rc = EVP_DigestVerifyFinal(ctx, sig, slen);
        if(rc != 1) {
            break; /* failed */
        }

        result = 0;

    } while(0);

    if(ctx) {
        EVP_MD_CTX_destroy(ctx);
        ctx = NULL;
    }

    return !!result;

}
