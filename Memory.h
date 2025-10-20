#ifndef MEMUNIT_H
#define MEMUNIT_H

#include <cstdint>

class MemUnit {
    public:
        MemUnit();
        int32_t read(uint32_t address);
        void write(uint32_t address, int32_t data);
    private:
        uint8_t memory[4096];
};

#endif // MEMUNIT_H