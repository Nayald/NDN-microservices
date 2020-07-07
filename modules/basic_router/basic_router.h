#pragma once

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <boost/asio.hpp>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "rapidjson/document.h"

#include "module.h"
#include "network/face.h"
#include "network/master_face.h"
#include "pit.h"
#include "fib.h"

class NameRouter : public Module {
    const std::string _name;

    Pit _pit;
    Fib _fib;

    char _command_buffer[65536];
    boost::asio::ip::udp::socket _command_socket;
    boost::asio::ip::udp::endpoint _remote_command_endpoint;

    ndn::KeyChain _keychain;
    size_t _request_id = 1;
    std::map<size_t, std::function<void(bool)>> _requests;
    std::map<size_t, std::shared_ptr<boost::asio::deadline_timer>> _request_timers;
    boost::asio::ip::udp::endpoint _manager_endpoint;
    bool _check_prefix = false;

    std::unordered_map<size_t, std::shared_ptr<Face>> _egress_faces;
    std::shared_ptr<MasterFace> _tcp_master_face;
    std::shared_ptr<MasterFace> _udp_master_face;

public:
    NameRouter(const std::string &name, uint16_t local_port, uint16_t local_command_port);

    ~NameRouter() override = default;

    void run() override;

    void onInterest(const std::shared_ptr<Face> &producer_face, const ndn::Interest &interest);

    void handleCommandInterest(const std::shared_ptr<Face> &producer_face, const ndn::Interest &interest);

    void handleOtherInterest(const std::shared_ptr<Face> &producer_face, const ndn::Interest &interest);

    void onManagerValidation(const std::shared_ptr<Face> &producer_face, const ndn::Interest &interest, const ndn::Name &prefix, bool result);

    void onTimeout(const boost::system::error_code &err, size_t index);

    void onData(const std::shared_ptr<Face> &producer_face, const ndn::Data &data);

    void onMasterFaceNotification(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onMasterFaceError(const std::shared_ptr<MasterFace> &master_face, const std::shared_ptr<Face> &face);

    void onFaceError(const std::shared_ptr<Face> &face);

    void commandRead();

    void commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void commandReply(const rapidjson::Document &document);

    void commandEditConfig(const rapidjson::Document &document);

    void commandAddFace(const rapidjson::Document &document);

    void commandDelFace(const rapidjson::Document &document);

    void commandAddRoutes(const rapidjson::Document &document);

    void commandDelRoutes(const rapidjson::Document &document);

    void commandList(const rapidjson::Document &document);
};