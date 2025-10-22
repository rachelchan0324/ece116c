#include "MUX.h"

// simple two-input multiplexer
int32_t MUX::mux2(int32_t input0, int32_t input1, bool select) {
    return select ? input1 : input0;
}

// three-input multiplexer for pc source selection
// select: 0 = pc+4, 1 = branch target, 2 = jump target
uint32_t MUX::mux3_pc(uint32_t pc_plus_4, uint32_t branch_target, uint32_t jump_target, int select) {
    switch(select) {
        case 1: return branch_target;
        case 2: return jump_target;
        default: return pc_plus_4;
    }
}