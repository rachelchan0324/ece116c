#ifndef MUX_H
#define MUX_H

#include <cstdint>

class MUX {
public:
    // two-input mux: select = 0 returns input0, select = 1 returns input1
    static int32_t mux2(int32_t input0, int32_t input1, bool select);
    
    // three-input mux for pc source selection
    static uint32_t mux3_pc(uint32_t pc_plus_4, uint32_t branch_target, uint32_t jump_target, int select);
};

#endif