#include <bitset>

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
    ALU_OP_INVALID,
};

class Controller {
    public:
        Controller();
        void setControlSignals(uint32_t opcode);
        ALUOp getALUOp() { return aluOp; }
        bool readSignal(ControlSignals signal) { return signals[signal]; }
    private:
        bool signals[6];
        ALUOp aluOp;
};