#include <ndn-cxx/common.hpp>

#include "pit.h"
#include "backward_router.h"
#include "log/logger.h"

static bool stop = false;
static void signal_handler(int signum) {
    stop = true;
}

int main(int argc, char *argv[]) {
    std::string name = "";
    size_t size = 0;
    uint16_t local_port = 0;
    uint16_t local_command_port = 0;

    char flags = 0;
    for (int i = 1; i < argc; i += 2) {
        switch (argv[i][1]) {
            case 'n':
                name = argv[i + 1];
                flags |= 0x1;
                break;
            case 's':
                size = std::atoi(argv[i + 1]);
                flags |= 0x2;
                break;
            case 'p':
                local_port = std::atoi(argv[i + 1]);
                flags |= 0x4;
                break;
            case 'C':
                local_command_port = std::atoi(argv[i + 1]);
                flags |= 0x8;
                break;
            case 'h':
            default:
                exit(0);
                break;
        }
    }
    if (flags != 0xF) {
        exit(-1);
    }

    std::cout << "backward router v1.0" << std::endl;

    logger::setFilename("logs.txt");
    logger::isTee(true);
    logger::setMinimalLogLevel(logger::INFO);

    BackwardRouter backward_router(name, size, local_port, local_command_port);
    backward_router.start();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    do {
        sleep(15);
    } while (!stop);

    backward_router.stop();

    return 0;
}
