#define TESLA_INIT_IMPL
#include "keyboard.hpp"

int main(int argc, char* argv[]) {
    tsl::g_argc = argc;
    tsl::g_argv = argv;
    return tsl::loop<tsl::KeyboardOverlay>(argc, argv);
}
