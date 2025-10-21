#include "CPU.h"
#include "InstructionParts.h"
#include "ALU.h"
#include "Controller.h"
#include <iostream>
#include <iomanip>

CPU::CPU() {
	current_PC = 0;
	next_PC = 0;
}

unsigned long CPU::readPC() {
	return current_PC;
}

void CPU::updateCurrentFromNext() {
	current_PC = next_PC;
}

int32_t CPU::readRegister(int regNum) {
	return regFile.read(regNum);
}

uint32_t CPU::fetch(char *instMem) {
	uint8_t byte0 = instMem[current_PC];
	uint8_t byte1 = instMem[current_PC + 1];
	uint8_t byte2 = instMem[current_PC + 2];
	uint8_t byte3 = instMem[current_PC + 3];

	// convert to big endian
	uint32_t currentInstruction = (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
	return currentInstruction;
}

InstructionParts CPU::decode(uint32_t instruction) {
	InstructionParts parts;
	parts.opcode = instruction & 0x7F; // opcode is in bits [6:0] (7 bits)
	parts.funct3 = instruction >> 12 & 0x07; // funct3 is in bits [14:12]
	parts.funct7 = instruction >> 25 & 0x7F; // funct7 is in bits [31:25]
	parts.rs1 = instruction >> 15 & 0x1F; // rs1 register NUMBER is in bits [19:15]
	parts.rs2 = instruction >> 20 & 0x1F; // rs2 register NUMBER is in bits [24:20]
	parts.rd = instruction >> 7 & 0x1F; // rd register NUMBER is in bits [11:7]
	parts.immediate = immGen.generate(instruction); // decode immediate based on instruction type	
	return parts;
}

bool CPU::execute(InstructionParts parts) {
	controller.setControlSignals(parts.opcode);

	// determine ALU operation
	ALUOp aluOp = controller.getALUOp();
	if (aluOp == ALU_OP_INVALID) {
		return false;
	}

	ALUOperation aluOperation = aluController.getALUOperation(aluOp, parts);
	if (aluOperation == ALU_INVALID) {
		return false;
	}

	// determine operands
	int32_t rs1_data = regFile.read(parts.rs1); // Read register values when needed
	int32_t rs2_data = regFile.read(parts.rs2);
	
	int32_t result;
	if (controller.getSignal(ControlSignals::AluSrc)) {
		result = alu.compute(rs1_data, parts.immediate, aluOperation);
	} else {
		result = alu.compute(rs1_data, rs2_data, aluOperation);
	}

	// check memory signals
	if (controller.getSignal(ControlSignals::MemWrite)) {
		memory.write(result, rs2_data);
	}

	if (controller.getSignal(ControlSignals::MemRead)) {
		// check the funct3 field to distinguish between load types
		if (parts.funct3 == 0x4) { // LBU (Load Byte Unsigned)
			uint8_t byte_data = memory.readByte(result);
			result = static_cast<int32_t>(byte_data); // zero-extend the byte
		} else if (parts.funct3 == 0x2) { // LW (Load Word)
			result = memory.read(result);
		}
	}

	if(controller.getSignal(ControlSignals::RegWrite)) {
		if(controller.getSignal(ControlSignals::Branch)) {
			if (result != 0) {
				next_PC = current_PC + parts.immediate;	// take the branch
				regFile.write(parts.rd, result);
				return true; // skip default PC += 4
			}
		}
		if (controller.getSignal(ControlSignals::Link)) {
			int32_t returnAddress = current_PC + 4;
			next_PC = result  & ~1; // ensure LSB is 0
			regFile.write(parts.rd, returnAddress);
			return true; // skip default PC += 4
		}
		regFile.write(parts.rd, result);
	}

	next_PC = current_PC + 4; // increment PC to next sequential instruction
	return true;
}

void CPU::printAllRegisters() {
	cout << "=== Register Contents ===" << endl;
	
	// print registers in a nice format
	for (int i = 0; i < 32; i++) {
		int32_t value = regFile.read(i);
		cout << "x" << setw(2) << setfill('0') << i << ": " 
		     << setw(10) << setfill(' ') << value 
		     << " (0x" << hex << setw(8) << setfill('0') << (uint32_t)value << dec << ")";
		
		// add register names for a0 and a1 specifically
		if (i == 10) cout << " [a0]";
		else if (i == 11) cout << " [a1]";
		
		cout << endl;
	}
	cout << "========================" << endl;
}