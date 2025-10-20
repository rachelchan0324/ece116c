#include "CPU.h"
#include "InstructionParts.h"
#include "ALU.h"
#include "Controller.h"

CPU::CPU()
{
	PC = 0; //set PC to 0
	for (int i = 0; i < 4096; i++) //copy instrMEM
	{
		dmemory[i] = (0);
	}
}

unsigned long CPU::readPC()
{
	return PC;
}
void CPU::incPC()
{
	PC++;
}

uint32_t CPU::fetch(char *instMem)
{
	uint8_t byte0 = instMem[PC];
	uint8_t byte1 = instMem[PC + 1];
	uint8_t byte2 = instMem[PC + 2]; 
	uint8_t byte3 = instMem[PC + 3];

	// convert to big endian
	uint32_t currentInstruction = (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
	return currentInstruction;
}

InstructionParts CPU::decode(uint32_t instruction)
{
	InstructionParts parts;
	parts.opcode = instruction & 0xFF; // opcode is in bits [6:0]
	parts.funct3 = instruction >> 12 & 0x07; // funct3 is in bits [14:12]
	parts.funct7 = instruction >> 25 & 0x7F; // funct7 is in bits [31:25]
	parts.rs1 = regFile.read(instruction >> 15 & 0x1F); // rs1 is in bits [19:15]
	parts.rs2 = regFile.read(instruction >> 20 & 0x1F); // rs2 is in bits [24:20]
	parts.rd = instruction >> 7 & 0x1F; // rd is in bits [11:7]
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

	ALUOperation aluOperation = alu.getALUOperation(aluOp, parts);
	if (aluOperation == ALU_INVALID) {
		return false;
	}

	// determine operands
	int32_t result;
	if (controller.getSignal(ControlSignals::AluSrc)) {
		result = alu.compute(parts.rs1, parts.immediate, aluOperation); // use immediate as second operand
	} else {
		result = alu.compute(parts.rs1, parts.rs2, aluOperation);
	}

	// check memory signals
	// if (controller.getSignal(ControlSignals::MemWrite)) {
	// 	result = dataMemory.read(result);
	// }

	return true;
}