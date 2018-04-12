#include <ndn-cxx/common.hpp>

#include "signature_verifier.h"
#include "log/logger.h"

static bool stop = false;
static void signal_handler(int signum) {
    stop = true;
}

int main(int argc, char *argv[]) {
    logger::setFilename("logs.txt");
    logger::setMinimalLogLevel(logger::Level::INFO);

    boost::asio::io_service ios;
    boost::asio::io_service::work work(ios);

    SignatureVerifier signature_verifier(ios, 6362, "127.0.0.1", 6363);
    signature_verifier.start();

    signal(SIGINT, signal_handler);

    do {
        ios.run();
    } while(!stop);

    return 0;
}