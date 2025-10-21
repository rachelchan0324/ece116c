#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <bitset>
#include <cstdint>

enum ControlSignals {
    RegWrite,
    AluSrc,
    Branch,
    MemRead,
    MemWrite,
    MemToReg,
};

enum ALUOp {
    ALU_OP_ADD,
    ALU_OP_SUB,
    ALU_OP_FUNC,
    ALU_OP_I_TYPE,
    ALU_OP_PASS_IMM,
    ALU_OP_INVALID,
};

class Controller {
    public:
        Controller();
        void setControlSignals(uint32_t opcode);
        ALUOp getALUOp() { return aluOp; }
        bool getSignal(ControlSignals signal) { return signals[signal]; }
    private:
        bool signals[6];
        ALUOp aluOp;
};

#endif // CONTROLLER_H