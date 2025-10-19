#include "CPU.h"
#include "InstructionParts.h"
#include "ALU.h"

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
	parts.rs1 = instruction >> 15 & 0x1F; // rs1 is in bits [19:15]
	parts.rs2 = instruction >> 20 & 0x1F; // rs2 is in bits [24:20]
	parts.rd = instruction >> 7 & 0x1F; // rd is in bits [11:7]
	parts.immediate = instruction >> 20; // immediate value (for I-type)

	// R-type instructions (opcode 0x33)
	if (parts.opcode == 0x33){
		if (parts.funct3 == 0x0 && parts.funct7 == 0x20){
			parts.alu_op = ALUOperation::ALU_SUB;
		}
		else if (parts.funct3 == 0x7){
			parts.alu_op = ALUOperation::ALU_AND;
		}
		else if (parts.funct3 == 0x5 && parts.funct7 == 0x20){
			parts.alu_op = ALUOperation::ALU_SRA;
		}
	}
	// I-type instructions (opcode 0x13) 
	else if (parts.opcode == 0x13){
		if (parts.funct3 == 0x0){
			parts.alu_op = ALUOperation::ALU_ADDI;
		}
		else if (parts.funct3 == 0x6){
			parts.alu_op = ALUOperation::ALU_ORI;
		}
		else if (parts.funct3 == 0x3){
			parts.alu_op = ALUOperation::ALU_SLTIU;
		}
	}
	// U-type instructions (opcode 0x37)
	else if (parts.opcode == 0x37){
		parts.alu_op = ALUOperation::ALU_LUI;
	}
	// load instructions (opcode 0x03)
	else if (parts.opcode == 0x03){
		if (parts.funct3 == 0x2){
			parts.alu_op = ALUOperation::ALU_LW;
		}
		else if (parts.funct3 == 0x4){
			parts.alu_op = ALUOperation::ALU_LBU;
		}
	}
	// store instructions (opcode 0x23)
	else if (parts.opcode == 0x23){
		// S-type immediate calculation
		auto imm11_5 = (int32_t) (instruction & 0xfe000000); // gets upper 7 bits
		auto imm4_0 = (int32_t) ((instruction & 0xf80) << 13); // gets lower 5 bits
		parts.immediate = (imm11_5 + imm4_0) >> 20;
		
		if (parts.funct3 == 0x1){
			parts.alu_op = ALUOperation::ALU_SH;
		}
		else if (parts.funct3 == 0x2){
			parts.alu_op = ALUOperation::ALU_SW;
		}
	}
	// branch instructions (opcode 0x63)
	else if (parts.opcode == 0x63){
		if (parts.funct3 == 0x1){
			parts.alu_op = ALUOperation::ALU_BNE;
		}
	}
	// jump instructions (opcode 0x67)
	else if (parts.opcode == 0x67){
		if (parts.funct3 == 0x0){
			parts.alu_op = ALUOperation::ALU_JALR;
		}
	}
	else {
		parts.alu_op = ALUOperation::ALU_INVALID; // Invalid operation
	}

	return parts;
}