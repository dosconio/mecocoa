#include "aaaaa.h"
#include "../include/syscall.hpp"
#include <c/ustring.h>

int main(int argc, char** argv) {
	// Initialize default arguments with zeros
	stduint args[4] = {0, 0, 0, 0};
	
	// argv[0] is typically the executable name, so actual arguments start from argv[1].
	// Limit the maximum number of processed arguments to 4.
	int num_args = argc - 1;
	if (num_args > 4) {
		num_args = 4;
	}
	if (num_args < 0) {
		num_args = 0;
	}
	
	// Parse each provided argument from string to integer
	for (int i = 0; i < num_args; i++) {
		args[i] = atoins(argv[i + 1]);
	}
	
	// Sequentially pass the integers to syscall.
	// args[0] is used as the syscall ID (callid), and args[1] through args[3] are the parameters.
	stduint result = syscall((syscall_t)args[0], args[1], args[2], args[3]);
	
	return result;
}
