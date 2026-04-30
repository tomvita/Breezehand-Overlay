#include "disasm.hpp"

#if !defined(BREEZEHAND_LITE)
#include <capstone.h>

namespace air {
    static csh handle = 0;
    static bool initialized = false;

    std::string DisassembleARM64(uint32_t code, uint64_t address) {
        if (!initialized) {
            if (cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle) != CS_ERR_OK) {
                return "ERR: CS INIT FAIL";
            }
            // We don't need details for basic mnemonics/op_str, keeping it off saves memory
            cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);
            initialized = true;
        }

        cs_insn *insn;
        size_t count;
        std::string result = "";

        count = cs_disasm(handle, (const uint8_t *)&code, 4, address, 1, &insn);
        if (count > 0) {
            result = std::string(insn[0].mnemonic) + " " + insn[0].op_str;
            cs_free(insn, count);
        }

        return result;
    }
}
#else
// Lite build: capstone is not linked. Provide a stub so callers compile and
// link without pulling in the disassembler. All in-overlay disassembly views
// (cheat-edit notes, asm preview) become empty strings, which the existing
// fallback paths in main.cpp already handle.
namespace air {
    std::string DisassembleARM64(uint32_t /*code*/, uint64_t /*address*/) {
        return std::string();
    }
}
#endif
