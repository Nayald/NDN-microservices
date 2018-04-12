#pragma once

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>

#include <boost/asio.hpp>

#include <memory>
#include <vector>
#include <set>

#include "rapidjson/document.h"

#include "module.h"
#include "network/face.h"
#include "network/master_face.h"

class PacketDispatcher : public Module {
private:
    class Session : public std::enable_shared_from_this<Session> {
    private:
        static size_t session_count;

        size_t _session_id;

        PacketDispatcher &_packet_dispatcher;

        std::shared_ptr<Face> _bidirectionnal_face;
        std::shared_ptr<Face> _consumer_face;
        std::shared_ptr<Face> _producer_face;

    public:
        Session(PacketDispatcher &packet_dispatcher, boost::asio::ip::tcp::socket&& socket);

        ~Session();

        void start();

        void stop();

        void onInterest(const std::shared_ptr<Face> &face, const ndn::Interest &interest);

        void onData(const std::shared_ptr<Face> &face, const ndn::Data &data);

        void onInterest2(const std::shared_ptr<Face> &face, const ndn::Interest &interest);

        void onData2(const std::shared_ptr<Face> &face, const ndn::Data &data);

        void onFaceError(const std::shared_ptr<Face> &egress_face);
    };

    bool id_set = false;
    size_t _module_id = 0;

    char _command_buffer[65536];
    boost::asio::ip::udp::socket _command_socket;
    boost::asio::ip::udp::endpoint _remote_command_endpoint;

    boost::asio::ip::tcp::acceptor _acceptor;
    boost::asio::ip::tcp::socket _acceptor_socket;

    std::string _consumer_layer;
    std::string _consumer_remote_ip;
    uint16_t _consumer_remote_port;

    std::string _producer_layer;
    std::string _producer_remote_ip;
    uint16_t _producer_remote_port;

    std::set<std::shared_ptr<Session>> _sessions;
    std::vector<std::shared_ptr<MasterFace>> _ingress_master_faces;

public:
    PacketDispatcher();

    ~PacketDispatcher() override = default;

    void run() override;

    void accept();

    void acceptHandler(const boost::system::error_code &err);

    void commandRead();

    void commandReadHandler(const boost::system::error_code &err, size_t bytes_transferred);

    void commandEditConfig(const rapidjson::Document &document);

    void commandAddFace(const rapidjson::Document &document);

    void commandAddMasterFace(const rapidjson::Document &document);

    void commandDelFace(const rapidjson::Document &document);

    void commandDelMasterFace(const rapidjson::Document &document);

    void commandList(const rapidjson::Document &document);
};