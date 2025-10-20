#include "RegFile.h"

RegFile::RegFile() {
    for (int i = 0; i < 32; i++) {
        registers[i] = 0;
    }
}

int32_t RegFile::read(uint8_t regNum) {
    return registers[regNum];
}

void RegFile::write(uint8_t regNum, int32_t data) {
    if (regNum != 0) { // register x0 is always 0
        registers[regNum] = data;
    }
}