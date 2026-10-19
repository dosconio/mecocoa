//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>
#include <sys/mman.h>
#include <stdio.h>

using namespace uni;

int main(int argc, char** argv)
{

	#if 1 // MMAP FILE
	{
		auto tmp = (char*)malloc(0x1001);
		*tmp = 'a';
		free(tmp);

		outsfmt("=== File MMAP Test Start ===\n\r");
		rostr test_file = "/md0/mmap_t.txt";

		// 1. Create and write to the test file
		int fd = sysopen(test_file);
		if (fd < 0) {
			fd = sys_createfil(test_file);
		}
		if (fd < 0) {
			outsfmt("Error: Failed to create test file %s!\n\r", test_file);
			return -1;
		}

		const char* init_content = "Hello Mecocoa File MMAP!";
		int written = write(fd, (void*)init_content, 24);
		static const char padding[4096 - 24] = {};
		static const char second_page[] = "Second page mmap";
		int padded = write(fd, padding, sizeof(padding));
		int second_written = write(fd, second_page, sizeof(second_page) - 1);
		outsfmt("Step 1: Write init pattern, fd=%d, bytes=%d\n\r", fd, written);
		if (written != 24 || padded != sizeof(padding) ||
			second_written != sizeof(second_page) - 1) {
			outsfmt("Error: Failed to prepare second file page!\n\r");
			close(fd);
			return -1;
		}
		close(fd);

		// 2. Re-open and establish mmap mapping
		fd = sysopen(test_file);
		if (fd < 0) {
			outsfmt("Error: Failed to re-open test file!\n\r");
			return -1;
		}

		// Map a 4KB page with flag 7 (present | writable | user)
		void* map_addr = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, fd, 0);
		outsfmt("Step 2: MMAP completed, map_addr=0x%x\n\r", (stduint)map_addr);
		if (map_addr == MAP_FAILED) {
			outsfmt("Error: MMAP failed!\n\r");
			close(fd);
			return -1;
		}

		// 3. Immediately close original fd to verify lifecycle independence (inode ref count)
		close(fd);
		outsfmt("Step 3: Closed original fd. Attempting to read from map...\n\r");

		// 4. First-time read and verify
		char* ptr = (char*)map_addr;
		char read_buf[32] = {0};
		for (int i = 0; i < 24; i++) {
			read_buf[i] = ptr[i];
		}
		outsfmt("Step 4: Read from memory: '%s'\n\r", read_buf);

		bool match = true;
		for (int i = 0; i < 24; i++) {
			if (read_buf[i] != init_content[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			outsfmt("Success: Initial memory contents match!\n\r");
		} else {
			outsfmt("Error: Memory contents mismatch!\n\r");
		}

		// 5. Modify mapped memory ("Hello" -> "World")
		outsfmt("Step 5: Modifying mapped memory ('Hello' -> 'World')...\n\r");
		ptr[0] = 'W';
		ptr[1] = 'o';
		ptr[2] = 'r';
		ptr[3] = 'l';
		ptr[4] = 'd';

		// 6. Unmap memory (munmap / UMAP)
		outsfmt("Step 6: Unmapping memory...\n\r");
		int umap_res = munmap(map_addr, 4096);
		if (umap_res != 0) {
			outsfmt("Error: UMAP failed with code %d!\n\r", umap_res);
		} else {
			outsfmt("Success: UMAP success!\n\r");
		}

		// 7. Re-open the file to verify data writeback
		fd = sysopen(test_file);
		if (fd < 0) {
			outsfmt("Error: Failed to open test file for writeback verification!\n\r");
			return -1;
		}

		char verify_buf[32] = {0};
		int read_bytes = read(fd, verify_buf, 24);
		outsfmt("Step 7: Read back from file: '%s' (bytes=%d)\n\r", verify_buf, read_bytes);
		close(fd);

		const char* expected_content = "World Mecocoa File MMAP!";
		match = true;
		for (int i = 0; i < 24; i++) {
			if (verify_buf[i] != expected_content[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			outsfmt("Success: File contents correctly written back!\n\r");
		} else {
			outsfmt("Error: File contents NOT written back properly!\n\r");
			return -1;
		}

		// 8. Map the second file page and verify its contents and writeback.
		fd = sysopen(test_file);
		if (fd < 0) return -1;
		void* second_map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_SHARED, fd, 4096);
		close(fd);
		if (second_map == MAP_FAILED) {
			outsfmt("Error: MMAP at file offset 4096 failed!\n\r");
			return -1;
		}
		char* second_ptr = (char*)second_map;
		for (stduint i = 0; i < sizeof(second_page) - 1; i++) {
			if (second_ptr[i] != second_page[i]) {
				outsfmt("Error: Second file page contents mismatch!\n\r");
				munmap(second_map, 4096);
				return -1;
			}
		}
		second_ptr[0] = 's';
		if (munmap(second_map, 4096) != 0) {
			outsfmt("Error: Second file page UMAP failed!\n\r");
			return -1;
		}
		fd = sysopen(test_file);
		if (fd < 0 || lseek(fd, 4096, SEEK_SET) != 4096) {
			outsfmt("Error: Failed to seek to second file page!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		char second_verify[sizeof(second_page)] = {};
		int second_read = read(fd, second_verify, sizeof(second_page) - 1);
		close(fd);
		if (second_read != sizeof(second_page) - 1 || second_verify[0] != 's') {
			outsfmt("Error: Second file page writeback failed!\n\r");
			return -1;
		}
		for (stduint i = 1; i < sizeof(second_page) - 1; i++) {
			if (second_verify[i] != second_page[i]) {
				outsfmt("Error: Second file page writeback contents mismatch!\n\r");
				return -1;
			}
		}
		outsfmt("Success: File offset 4096 mapped and written back!\n\r");
		outsfmt("=== File MMAP Test ALL PASSED! ===\n\r");

		// 9. Clean up the temporary test file
		sys_removefil(test_file);
		outsfmt("Step 9: Cleaned up test file.\n\r");
	}
	#endif

	// loop sysrest(0, 0);
	return 0;
}

