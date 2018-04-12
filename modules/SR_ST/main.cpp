#include <ndn-cxx/common.hpp>

#include "strategy_router.h"
#include "log/logger.h"

static bool stop = false;
static void signal_handler(int signum) {
    stop = true;
}

int main(int argc, char *argv[]) {
    std::string name = "";
    uint16_t local_port = 0;
    uint16_t local_command_port = 0;

    char flags = 0;
    for (int i = 1; i < argc; i += 2) {
        switch (argv[i][1]) {
            case 'n':
                name = argv[i + 1];
                flags |= 0x1;
                break;
            case 'p':
                local_port = std::atoi(argv[i + 1]);
                flags |= 0x2;
                break;
            case 'C':
                local_command_port = std::atoi(argv[i + 1]);
                flags |= 0x4;
                break;
            case 'h':
            default:
                exit(0);
                break;
        }
    }
    if (flags != 0x7) {
        exit(-1);
    }

    std::cout << "strategy router v1.0" << std::endl;

    logger::setFilename("logs.txt");
    logger::isTee(true);
    logger::setMinimalLogLevel(logger::INFO);

    StrategyRouter strategy_router(name, local_port, local_command_port);
    strategy_router.start();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    do {
        sleep(15);
    }while(!stop);

    strategy_router.stop();

    return 0;
}