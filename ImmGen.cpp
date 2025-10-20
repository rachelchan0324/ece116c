#include "ImmGen.h"
#include <bitset>

int32_t ImmGen::generate(uint32_t instruction) {
    uint8_t opcode = instruction & 0x7F; // bits [6:0]
    int32_t immediate = 0;

    switch (opcode) {
        case 0x13: // I-type
            immediate = (instruction >> 20) & 0xFFF; // bits [31:20]
            if (immediate & 0x800) {
                immediate |= 0xFFFFF000;
            }
            break;
        case 0x23: // S-type
            immediate = ((instruction >> 25) & 0x7F) << 5 | ((instruction >> 7) & 0x1F); // bits [31:25] and [11:7]
            if (immediate & 0x800) {
                immediate |= 0xFFFFF000;
            }
            break;
        case 0x63: // B-type
            immediate = ((instruction >> 31) & 0x1) << 12 | ((instruction >> 7) & 0x1) << 11 |
                        ((instruction >> 25) & 0x3F) << 5 | ((instruction >> 8) & 0xF) << 1; // bits [31], [7], [30:25], [11:8]
            if (immediate & 0x1000) {
                immediate |= 0xFFFFE000;
            }
            break;
        case 0x37: // U-type (LUI)
            immediate = (instruction >> 12) & 0xFFFFF; // bits [31:12] - 20 bits
            immediate <<= 12; // shift to upper 20 bits, lower 12 bits become 0
            break;
        default:
            immediate = 0; // for R-type and other types without immediates
            break;
    }

    return immediate;
}