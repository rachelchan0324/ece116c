#ifndef ALU_H
#define ALU_H

#include "InstructionParts.h"
#include "Controller.h"

// unique ALU operations that we just need
enum ALUOperation {
    ALU_ADD,
    ALU_SUB,
    ALU_AND,
    ALU_OR,
    ALU_SLTU,
    ALU_SRA,
    ALU_INVALID
};

// performs arithmetic and logic operations
class ALU {
    public:
        ALU();
        int32_t compute(int32_t operand1, int32_t operand2, ALUOperation operation);
    private:
        bool AluSrc;
};

// generates correct ALU operation
class ALUController {
    public:
        ALUOperation getALUOperation(ALUOp aluOp, InstructionParts parts);
};

#endif