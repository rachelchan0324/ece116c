#ifndef INSTRUCTIONPARTS_H
#define INSTRUCTIONPARTS_H

#include <stdint.h>
#include "ALU.h"

struct InstructionParts {
    uint8_t opcode;      // 7 bits: bits [6:0]
    uint8_t funct3;      // 3 bits: bits [14:12]  
    uint8_t funct7;      // 7 bits: bits [31:25]
    uint8_t rs1;         // 5 bits: bits [19:15]
    uint8_t rs2;         // 5 bits: bits [24:20]
    uint8_t rd;          // 5 bits: bits [11:7]
    int32_t immediate;   // up to 32 bits, signed
    ALUOperation alu_op;  // The ALU operation to perform
};

#endif