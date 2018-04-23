#pragma once

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <boost/asio.hpp>

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <memory>
#include <string>
#include <queue>
#include <map>
#include <set>

#include "rapidjson/document.h"

#include "module.h"
#include "network/master_face.h"
#include "network/face.h"
#include "rapidjson/document.h"

class SignatureVerifier : public Module {
private:
    const std::string _name;

    std::vector<std::shared_ptr<Face>> _egress_faces;
    std::shared_ptr<MasterFace> _tcp_ingress_master_face;
    std::shared_ptr<MasterFace> _udp_ingress_master_face;

    char _command_buffer[65536];
    boost::asio::ip::udp::socket _command_socket;
    boost::asio::ip::udp::endpoint _remote_command_endpoint;
    boost::asio::ip::udp::endpoint _manager_endpoint;

    bool _report_enable = false;
    boost::asio::deadline_timer _report_timer;
    boost::posix_time::milliseconds _delay_between_report;

    bool _drop = false;
    bool _no_key_drop = false;
    bool _unsigned_drop = false;
    std::set<std::string> _invalid_signature_packet_names;
    std::map<ndn::Name, EVP_PKEY*> _pkeys;

public:
    SignatureVerifier(const std::string &name, uint16_t local_port, uint16_t local_command_port);

    ~SignatureVerifier() override = default;

    void run() override;

private:
    void onIngressInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest);

    void onIngressData(const std::shared_ptr<Face> &face, const ndn::Data &data);

    void onEgressInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest);

    void onEgressData(const std::shared_ptr<Face> &face, const ndn::Data &data);

    void onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onFaceError(const std::shared_ptr<Face> &face);

    void commandRead();

    void commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void commandEditConfig(const rapidjson::Document &document);

    void commandAddFace(const rapidjson::Document &document);

    void commandDelFace(const rapidjson::Document &document);

    void commandAddKeys(const rapidjson::Document &document);

    void commandDelKeys(const rapidjson::Document &document);

    void commandList(const rapidjson::Document &document);

    void commandReport(const boost::system::error_code &err);

    int verify(const unsigned char* msg, size_t mlen, const unsigned char* sig, size_t slen, EVP_PKEY* pkey);
};