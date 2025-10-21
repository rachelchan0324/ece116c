#include "CPU.h"
#include "InstructionParts.h"

#include <iostream>
#include <bitset>
#include <stdio.h>
#include<stdlib.h>
#include <string>
#include<fstream>
#include <sstream>
using namespace std;

/*
Add all the required standard and developed libraries here
*/

/*
Put/Define any helper function/definitions you need here
*/
int main(int argc, char* argv[])
{
	/* This is the front end of your project.
	You need to first read the instructions that are stored in a file and load them into an instruction memory.
	*/

	/* Each cell should store 1 byte. You can define the memory either dynamically, or define it as a fixed size with size 4KB (i.e., 4096 lines). Each instruction is 32 bits (i.e., 4 lines, saved in little-endian mode).
	Each line in the input file is stored as an hex and is 1 byte (each four lines are one instruction). You need to read the file line by line and store it into the memory. You may need a mechanism to convert these values to bits so that you can read opcodes, operands, etc.
	*/

	char instMem[4096];

	if (argc < 2) {
		//cout << "No file name entered. Exiting...";
		return -1;
	}

	ifstream infile(argv[1]); //open the file
	if (!(infile.is_open() && infile.good())) {
		cout<<"error opening file\n";
		return 0; 
	}
	string line; 
	int i = 0;
	while (infile >> line) {
		stringstream line2(line);
		int x; 
		line2 >> hex >> x;
		instMem[i] = x;
		i++;
	}
	int maxPC= i; 

	/* Instantiate your CPU object here.  CPU class is the main class in this project that defines different components of the processor.
	CPU class also has different functions for each stage (e.g., fetching an instruction, decoding, etc.).
	*/

	CPU myCPU = CPU();  // call the approriate constructor here to initialize the processor... make sure to create a variable for PC and resets it to zero (e.g., unsigned int PC = 0); 
	
	int a0 =0;
	int a1 =0;  

	bool done = true;
	int count = 5;
	while (count > 0) // processor's main loop. Each iteration is equal to one clock cycle.  
	{
		uint32_t currentInstruction = myCPU.fetch(instMem); //fetch
		cout << "PC: " << myCPU.readPC() << " Instruction: 0x" << hex << currentInstruction << dec << endl;
		
		InstructionParts parts = myCPU.decode(currentInstruction); // decode
		cout << "Opcode: 0x" << hex << (int)parts.opcode << dec << endl;
		
		if (myCPU.execute(parts)) { // executes
			cout << "Execute successful" << endl;
			myCPU.incPC();
		} else {
			cout << "Execute failed - stopping" << endl;
			done = false; // stop execution
		}
		if (myCPU.readPC() > maxPC)
			break;

		myCPU.printAllRegisters(); // print all registers for debugging
		a0 = myCPU.readRegister(10); // read register a0 (x10)
		a1 = myCPU.readRegister(11); // read register a1 (x11)
		count--;
	}
	// print the results (you should replace a0 and a1 with your own variables that point to a0 and a1)
	cout << "(" << a0 << "," << a1 << ")" << endl;
	
	return 0;
}