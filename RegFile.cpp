#include "RegFile.h"

// initialize all registers to zero
RegFile::RegFile() {
    for (int i = 0; i < 32; i++) {
        registers[i] = 0;
    }
}

// read value from specified register
int32_t RegFile::read(uint8_t regNum) {
    return registers[regNum];
}

// write value to register, ensuring x0 stays zero
void RegFile::write(uint8_t regNum, int32_t data) {
    if (regNum != 0) { // register x0 is always 0
        registers[regNum] = data;
    }
}