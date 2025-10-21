#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>

class Memory {
    public:
        Memory();
        int32_t read(uint32_t address);
        uint8_t readByte(uint32_t address);
        void write(uint32_t address, int32_t data);
    private:
        uint8_t memory[131072]; // 128 KB memory
};

#endif // MEMORY_H