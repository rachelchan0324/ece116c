#ifndef REGFILE_H
#define REGFILE_H

#include <cstdint>

class RegFile {
    public:
        RegFile();
        int32_t read(uint8_t regNum);
        void write(uint8_t regNum, int32_t data);
    private:
        int32_t registers[32];
};

#endif // REGFILE_H