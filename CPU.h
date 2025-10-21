#ifndef CPU_H
#define CPU_H

#include <iostream>
#include <bitset>
#include <stdio.h>
#include<stdlib.h>
#include <string>
#include "InstructionParts.h"
#include "ImmGen.h"
#include "ALU.h"
#include "RegFile.h"
#include "Memory.h"
#include "Controller.h"
using namespace std;


// class instruction { // optional
// public:
// 	bitset<32> instr;//instruction
// 	instruction(bitset<32> fetch); // constructor

// };

class CPU {
public:
	CPU();
	unsigned long readPC();
	uint32_t fetch(char *instMem); // fetch the 32-bit instruction from instruction memory
	InstructionParts decode(uint32_t instruction); // decode the fetched instruction
	bool execute(InstructionParts parts); // execute the ALU instructions
	int32_t readRegister(int regNum); // read register value
	void printAllRegisters(); // debug function to print all register values
	void updateCurrentFromNext();
private:
	ImmGen immGen;
	ALU alu;
	ALUController aluController;
	Controller controller;
	RegFile regFile;
	Memory memory;

	unsigned long current_PC, next_PC;
};

#endif // CPU_H