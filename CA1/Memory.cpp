#include "Memory.h"
#include <iostream>
using namespace std;

// initialize memory array to all zeros
Memory::Memory() {
    for (int i = 0; i < 131072; i++) {
        memory[i] = 0;
    }
}

// read 32-bit word from memory in little endian format
int32_t Memory::read(uint32_t address) {
    int32_t data = 0;
    data |= (memory[address] & 0xFF);
    data |= (memory[address + 1] & 0xFF) << 8;
    data |= (memory[address + 2] & 0xFF) << 16;
    data |= (memory[address + 3] & 0xFF) << 24;
    
    return data;
}

// read single byte from memory for unsigned byte loads
uint8_t Memory::readByte(uint32_t address) {
    return memory[address];
}

// write 32-bit word to memory in little endian format
void Memory::write(uint32_t address, int32_t data) {
    memory[address] = (data & 0xFF);
    memory[address + 1] = (data >> 8) & 0xFF;
    memory[address + 2] = (data >> 16) & 0xFF;
    memory[address + 3] = (data >> 24) & 0xFF;
}