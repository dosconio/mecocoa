//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>

using namespace uni;

int main(int argc, char** argv)
{
	// malloc 1: correct allocation and free
	outsfmt("[TestApp] Performing malloc 1 (100 bytes)...\n\r");
	void* ptr1 = malloc(100);
	if (ptr1) {
		outsfmt("[TestApp] malloc 1 succeeded, address: %p\n\r", ptr1);
		// Write to it to trigger page fault (demand paging) and verify zero-filling
		bool zeroed = true;
		for (int i = 0; i < 100; i++) {
			if (((char*)ptr1)[i] != 0) {
				zeroed = false;
			}
		}
		outsfmt("[TestApp] malloc 1 memory zero-fill check: %s\n\r", zeroed ? "PASSED" : "FAILED");
		
		// Write unique pattern
		((char*)ptr1)[0] = 0xA5;
		
		// free 1: correct free
		outsfmt("[TestApp] Freeing malloc 1 with correct address...\n\r");
		free(ptr1);
		outsfmt("[TestApp] Freeing malloc 1 completed.\n\r");
	} else {
		outsfmt("[TestApp] malloc 1 failed!\n\r");
	}

	// free 2: incorrect address (offset from original ptr1 to test invalid deallocation detection)
	if (ptr1) {
		void* bad_ptr = (void*)((stduint)ptr1 + 4);
		outsfmt("[TestApp] Freeing incorrect address %p (should print Mempool error)...\n\r", bad_ptr);
		free(bad_ptr);
		outsfmt("[TestApp] Freeing incorrect address completed.\n\r");
	}

	// malloc 2: allocated and not freed (OS will clean up on exit)
	outsfmt("[TestApp] Performing malloc 2 (200 bytes, will NOT be freed)...\n\r");
	void* ptr2 = malloc(200);
	if (ptr2) {
		outsfmt("[TestApp] malloc 2 succeeded, address: %p\n\r", ptr2);
		// Write to trigger page fault
		((char*)ptr2)[0] = 0x5A;
		outsfmt("[TestApp] malloc 2 written successfully, leaving it for OS cleanup.\n\r");
	} else {
		outsfmt("[TestApp] malloc 2 failed!\n\r");
	}

	// Trigger access to unmapped area to demonstrate abnormal address display
	outsfmt("[TestApp] Accessing unmapped address 0x50000000 to trigger crash...\n\r");
	sysrest(1, 100); // Short delay to ensure output is flushed
	*((volatile char*)0x50000000) = 0xAA;

}

