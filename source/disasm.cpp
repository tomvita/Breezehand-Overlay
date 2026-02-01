#include "disasm.hpp"
#include <capstone.h>

namespace air {
    std::string DisassembleARM64(uint32_t code) {
        csh handle;
        cs_insn *insn;
        size_t count;
        std::string result = "";

        if (cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle) != CS_ERR_OK)
            return "";

        count = cs_disasm(handle, (const uint8_t *)&code, 4, 0, 1, &insn);
        if (count > 0) {
            result = std::string(insn[0].mnemonic) + " " + insn[0].op_str;
            cs_free(insn, count);
        }

        cs_close(&handle);
        return result;
    }
}
