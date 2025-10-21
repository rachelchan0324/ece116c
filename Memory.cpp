#include "Memory.h"

Memory::Memory() {
    for (int i = 0; i < 4096; i++) {
        memory[i] = 0;
    }
}

int32_t Memory::read(uint32_t address) {
    // reading 4 bytes from little endian memory
    int32_t data = 0;
    data |= (memory[address] & 0xFF);
    data |= (memory[address + 1] & 0xFF) << 8;
    data |= (memory[address + 2] & 0xFF) << 16;
    data |= (memory[address + 3] & 0xFF) << 24;
    return data;
}

void Memory::write(uint32_t address, int32_t data) {
    // storing 4 bytes in little endian memory
    memory[address] = (data & 0xFF);
    memory[address + 1] = (data >> 8) & 0xFF;
    memory[address + 2] = (data >> 16) & 0xFF;
    memory[address + 3] = (data >> 24) & 0xFF;
}