#include <ndn-cxx/common.hpp>

#include "packet_dispather.h"
#include "log/logger.h"

static bool stop = false;
static void signal_handler(int signum) {
    stop = true;
}

int main(int argc, char *argv[]) {
    logger::setFilename("logs.txt");
    logger::isTee(true);
    logger::setMinimalLogLevel(logger::INFO);

    PacketDispatcher packet_dispatcher;
    packet_dispatcher.start();

    signal(SIGINT, signal_handler);

    do {
        sleep(15);
    }while(!stop);

    packet_dispatcher.stop();

    return 0;
}