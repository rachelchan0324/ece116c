#include "Controller.h"

Controller::Controller() {
    for(int i = 0; i < 7; i++) {
        signals[i] = false;
    }
    aluOp = ALUOp::ALU_OP_INVALID;
}

void Controller::setControlSignals(uint32_t opcode) {
    switch (opcode) {
        case 0x33: // R-type
            signals[ControlSignals::RegWrite] = true;
            aluOp = ALUOp::ALU_OP_FUNC; // ALU operation determined by funct fields
            break;
        case 0x13: // I-type
            signals[ControlSignals::RegWrite] = true;
            signals[ControlSignals::AluSrc] = true;
            aluOp = ALUOp::ALU_OP_FUNC; // Check funct3 to determine operation
            break;
        case 0x03: // load
            signals[ControlSignals::RegWrite] = true;
            signals[ControlSignals::AluSrc] = true;
            signals[ControlSignals::MemRead] = true;
            signals[ControlSignals::MemToReg] = true;
            aluOp = ALUOp::ALU_OP_ADD;
            break;
        case 0x23: // store
            signals[ControlSignals::AluSrc] = true;
            signals[ControlSignals::MemWrite] = true;
            aluOp = ALUOp::ALU_OP_ADD;
            break;
        case 0x63: // branch
            signals[ControlSignals::Branch] = true;
            aluOp = ALUOp::ALU_OP_SUB;
            break;
        case 0x37: // LUI
            signals[ControlSignals::RegWrite] = true;
            signals[ControlSignals::AluSrc] = true;
            aluOp = ALUOp::ALU_OP_PASS_IMM;
            break;
        default:
            // Handle other opcodes or invalid opcode
            break;
    }
}