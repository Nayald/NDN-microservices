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

#include "network/master_face.h"
#include "network/face.h"

class SignatureVerifier {
private:
    boost::asio::io_service &_ios;

    std::shared_ptr<Face> _output_face;

    std::shared_ptr<MasterFace> _input_master_face;
    std::shared_ptr<Face> _input_face;

    boost::asio::ip::udp::socket _command_socket;
    char _command_buffer[65536];
    boost::asio::ip::udp::endpoint _remote_endpoint;

    std::map<ndn::Name, std::unique_ptr<EVP_PKEY>> _pkeys;

    bool _drop = false;
    bool _no_key_drop = false;
    std::set<std::string> _invalid_signature_packet_names;
    boost::asio::ip::udp::endpoint _report_endpoint;
    std::string _report_name;
    boost::asio::deadline_timer _report_timer;
    bool _report_enable = false;
    boost::posix_time::milliseconds _delay_between_report;

public:
    SignatureVerifier(boost::asio::io_service &ios, uint16_t local_port, const std::string &remote_address, uint16_t remote_port);

    ~SignatureVerifier();

    void start();

private:
    void onInputInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest);

    void onInputData(const std::shared_ptr<Face> &face, const ndn::Data &data);

    void onOutputInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest);

    void onOutputData(const std::shared_ptr<Face> &face, const ndn::Data &data);

    void onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onFaceError(const std::shared_ptr<Face> &face);

    void commandRead();

    void commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void commandReport(const boost::system::error_code &err);

    void refreshPKeys();
};