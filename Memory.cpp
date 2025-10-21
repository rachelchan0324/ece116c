#include "Memory.h"
#include <iostream>
using namespace std;

Memory::Memory() {
    for (int i = 0; i < 131072; i++) {
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

uint8_t Memory::readByte(uint32_t address) {
    // reading 1 byte from memory
    return memory[address];
}

void Memory::write(uint32_t address, int32_t data) {
    memory[address] = (data & 0xFF);
    memory[address + 1] = (data >> 8) & 0xFF;
    memory[address + 2] = (data >> 16) & 0xFF;
    memory[address + 3] = (data >> 24) & 0xFF;
}