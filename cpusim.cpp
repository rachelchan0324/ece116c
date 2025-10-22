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
// main cpu simulator function
int main(int argc, char* argv[]) {
	// load instruction file into memory and execute cpu simulation

	char instMem[4096];

	// check for command line argument
	if (argc < 2) {
		return -1;
	}

	// open and read instruction file
	ifstream infile(argv[1]);
	if (!(infile.is_open() && infile.good())) {
		cout<<"error opening file\n";
		return 0; 
	}
	
	// load instructions into memory as bytes
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

	// create cpu instance and initialize simulation variables

	CPU myCPU = CPU();
	
	int a0 =0;
	int a1 =0;  

	bool done = true;
	// main cpu simulation loop - each iteration represents one clock cycle
	while (done) {
		myCPU.updateCurrentFromNext(); // update state at start of cycle

		// fetch, decode, and execute instruction
		uint32_t currentInstruction = myCPU.fetch(instMem);
		InstructionParts parts = myCPU.decode(currentInstruction);
		
		if (myCPU.execute(parts)) {
			// execution successful
		} else {
			done = false; // stop execution on failure
		}
		
		// check if pc exceeds program bounds
		if (myCPU.readPC() > maxPC)
			break;

		// read final register values for output
		a0 = myCPU.readRegister(10); // read register a0 (x10)
		a1 = myCPU.readRegister(11); // read register a1 (x11)
	}
	// print final results in required format
	cout << "(" << a0 << "," << a1 << ")" << endl;
	
	return 0;
}