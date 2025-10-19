#ifndef ALU_H
#define ALU_H

enum ALUOperation {
    ALU_ADDI,
    ALU_LUI,
    ALU_ORI,
    ALU_SLTIU,
    ALU_SRA,
    ALU_SUB,
    ALU_AND,
    ALU_LBU,
    ALU_LW,
    ALU_SH,
    ALU_SW,
    ALU_BNE,
    ALU_JALR,
    ALU_INVALID,
};

class ALU {
    public:
        ALU();
        //int32_t compute(int32_t operand1, int32_t operand2, ALUOperation operation);
};

#endif