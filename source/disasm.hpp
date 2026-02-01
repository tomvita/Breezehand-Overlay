#pragma once
#include <string>
#include <cstdint>

namespace air {
    std::string DisassembleARM64(uint32_t code, uint64_t address);
}
