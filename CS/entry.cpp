#include "entry.h"

#include <boost/filesystem.hpp>

#include <fstream>

#include "entry_exception.h"
#include "file_exception.h"

std::string entry::root_dir;

entry::entry() { }

entry::entry(ndn::Data &data) : data_ptr(std::make_shared<ndn::Data>(data)), expire_time_point(
        boost::chrono::steady_clock::now() + data.getFreshnessPeriod()) { }


entry::entry(std::shared_ptr<ndn::Data> data) : data_ptr(data), expire_time_point(
        boost::chrono::steady_clock::now() + data->getFreshnessPeriod()) { }

entry::~entry() { }

bool entry::isValid() const {
    return expire_time_point > boost::chrono::steady_clock::now();
}

long entry::remaining() const {
    return boost::chrono::duration_cast<boost::chrono::milliseconds>(
            expire_time_point - boost::chrono::steady_clock::now()).count();
}

void entry::validFor(long milliseconds) {
    expire_time_point = boost::chrono::steady_clock::now() + boost::chrono::milliseconds(milliseconds);
}

std::shared_ptr<ndn::Data> entry::getData() const {
    return data_ptr;
}

void entry::storeToDisk() const {
    size_t dir_depth = data_ptr->getName().size();
    for (size_t i = 0; i < dir_depth; ++i) {
        if (boost::filesystem::exists(boost::filesystem::path(root_dir + data_ptr->getName().getSubName(0, i).toUri())))
            continue;
        if (!boost::filesystem::create_directory(
                boost::filesystem::path(root_dir + data_ptr->getName().getSubName(0, i).toUri())))
            throw entry_exception();
    }
    std::ofstream file(root_dir + data_ptr->getName().toUri());
    if (file) {
        long remaining_time = remaining();
        file.write((char *) &remaining_time, sizeof(long));
        size_t size = data_ptr->wireEncode().size();
        file.write((char *) &size, sizeof(size_t));
        const uint8_t *raw_packet = data_ptr->wireEncode().wire();
        file.write(reinterpret_cast<const char *>(raw_packet), size);
    } else {
        throw file_exception();
    }
}

void entry::removeFromDisk(ndn::Name name) {
    boost::filesystem::path path(root_dir + name.toUri());
    boost::filesystem::remove(path);
    while ((path = path.parent_path()) != root_dir) {
        if (!boost::filesystem::is_empty(path)) {
            break;
        } else {
            boost::filesystem::remove(path);
        }
    }
}

entry entry::getFromDisk(ndn::Name name) {
    std::ifstream file(root_dir + name.toUri());
    if (file) {
        long expire_time;
        size_t size;
        file.read((char *) &expire_time, sizeof(long));
        file.read((char *) &size, sizeof(size_t));
        std::vector<char> raw_packet(size);
        file.read(&raw_packet[0], size);
        removeFromDisk(name);
        auto data_ptr = std::make_shared<ndn::Data>(ndn::Block(&raw_packet[0], size));
        entry e(data_ptr);
        e.validFor(expire_time);
        return e;
    }
    return entry();
}

