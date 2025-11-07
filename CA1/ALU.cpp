#include "ALU.h"
#include "Controller.h"

// initialize alu with default settings
ALU::ALU() {AluSrc = false;}

// determine specific alu operation based on instruction type and fields
ALUOperation ALUController::getALUOperation(ALUOp aluOp, InstructionParts parts) {
    if (aluOp == ALU_OP_ADD) {
        return ALU_ADD; // for address calculations (loads, stores, jalr)
    } else if (aluOp == ALU_OP_SUB) {
        return ALU_SUB; // for branch comparisons
    } else if (aluOp == ALU_OP_PASS_IMM) {
        return ALU_COPY_IMM; // for lui instruction
    } else if (aluOp == ALU_OP_FUNC) {
        // check opcode and funct3 to determine specific operation

        if (parts.opcode == 0x13 && parts.funct3 == 0x0) {
            return ALU_ADD; // ADDI
        }

        switch (parts.funct3) {
            case 0x0: // SUB (R-type)
                if (parts.funct7 == 0x20) {
                    return ALU_SUB; // SUB instruction
                } else {
                    return ALU_INVALID;
                }
            case 0x7: // AND (R-type)
                return ALU_AND;
            case 0x5: // SRA (R-type with funct7 check)
                if (parts.funct7 == 0x20) {
                    return ALU_SRA; // SRA (Shift Right Arithmetic)
                } else {
                    return ALU_INVALID; // SRL not in requirements
                }
            case 0x3: // SLTIU (I-type) 
                return ALU_SLTU; // set less than unsigned
            case 0x6: // ORI (I-type)
                return ALU_OR; // OR with immediate
            default:
                return ALU_INVALID;
        }
    }
    return ALU_INVALID;
}

// perform arithmetic and logical computations
int32_t ALU::compute(int32_t operand1, int32_t operand2, ALUOperation operation) {
    switch(operation) {
        case ALU_ADD:
            return operand1 + operand2;
        case ALU_SUB:
            return static_cast<int32_t>(static_cast<uint32_t>(operand1) - static_cast<uint32_t>(operand2));
        case ALU_AND:
            return static_cast<int32_t>(static_cast<uint32_t>(operand1) & static_cast<uint32_t>(operand2));
        case ALU_OR:
            return static_cast<int32_t>(static_cast<uint32_t>(operand1) | static_cast<uint32_t>(operand2));
        case ALU_SLTU:
            return (static_cast<uint32_t>(operand1) < static_cast<uint32_t>(operand2)) ? 1 : 0;
        case ALU_SRA:
            return operand1 >> (operand2 & 0x1F); // shift amount is lower 5 bits
        case ALU_COPY_IMM:
            return operand2; // simply pass the immediate value
        default:
            return 0; // or some error code
    }
}