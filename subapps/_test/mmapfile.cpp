//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <fcntl.h>
#include <c/ISO_IEC_STD/signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uni;

int main(int argc, char** argv)
{
	if (argc > 1 && argv[1][0] == 'e') {
		outsfmt("Success: exec replacement image started.\n\r");
		return 0;
	}
	const bool fault_mode = argc > 1 && argv[1][0] == 'f';

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
		static const char third_page[] = "Third page mmap";
		int padded = write(fd, padding, sizeof(padding));
		int second_written = write(fd, second_page, sizeof(second_page) - 1);
		int second_padded = write(fd, padding, sizeof(padding));
		int second_tail = write(fd, padding,
			4096 - (sizeof(second_page) - 1) - sizeof(padding));
		int third_written = write(fd, third_page, sizeof(third_page) - 1);
		outsfmt("Step 1: Write init pattern, fd=%d, bytes=%d\n\r", fd, written);
		if (written != 24 || padded != sizeof(padding) ||
			second_written != sizeof(second_page) - 1 ||
			second_padded != sizeof(padding) ||
			second_tail != 4096 - (sizeof(second_page) - 1) - sizeof(padding) ||
			third_written != sizeof(third_page) - 1) {
			outsfmt("Error: Failed to prepare three file pages!\n\r");
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
			return -1;
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
			return -1;
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

		// 9. Keep simultaneous mappings, reuse a freed address, then split a VMA.
		fd = sysopen(test_file);
		if (fd < 0) return -1;
		void* first_map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_SHARED, fd, 0);
		void* independent_map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_SHARED, fd, 4096);
		close(fd);
		if (first_map == MAP_FAILED || independent_map == MAP_FAILED ||
			((stduint)first_map < (stduint)independent_map + 4096 &&
				(stduint)independent_map < (stduint)first_map + 4096)) {
			outsfmt("Error: Simultaneous MMAP ranges overlap or failed!\n\r");
			return -1;
		}
		if (((char*)first_map)[0] != 'W' || ((char*)independent_map)[0] != 's' ||
			munmap(first_map, 4096) != 0) {
			outsfmt("Error: First mapping read or UMAP failed!\n\r");
			return -1;
		}
		fd = sysopen(test_file);
		if (fd < 0) return -1;
		void* reused_map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_SHARED, fd, 0);
		close(fd);
		if (reused_map == MAP_FAILED || reused_map != first_map ||
			((char*)reused_map)[0] != 'W' || ((char*)independent_map)[0] != 's') {
			outsfmt("Error: MMAP did not reuse the freed address!\n\r");
			return -1;
		}
		fd = sysopen(test_file);
		if (fd < 0) return -1;
		void* range_map = mmap(nullptr, 3 * 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_SHARED, fd, 0);
		close(fd);
		if (range_map == MAP_FAILED ||
			((stduint)range_map < (stduint)reused_map + 4096 &&
				(stduint)reused_map < (stduint)range_map + 3 * 4096) ||
			((stduint)range_map < (stduint)independent_map + 4096 &&
				(stduint)independent_map < (stduint)range_map + 3 * 4096)) {
			outsfmt("Error: Three-page MMAP overlaps another mapping or failed!\n\r");
			return -1;
		}
		char* range_ptr = (char*)range_map;
		if (range_ptr[0] != 'W' || range_ptr[4096] != 's' ||
			((char*)independent_map)[0] != 's') {
			outsfmt("Error: Three-page MMAP initial contents mismatch!\n\r");
			return -1;
		}
		if (munmap(range_ptr + 4096, 4096) != 0) {
			outsfmt("Error: Middle-page UMAP failed!\n\r");
			return -1;
		}
		for (stduint i = 0; i < sizeof(third_page) - 1; i++) {
			if (range_ptr[8192 + i] != third_page[i]) {
				outsfmt("Error: Right VMA file offset mismatch after split!\n\r");
				return -1;
			}
		}
		if (range_ptr[0] != 'W') {
			outsfmt("Error: Left VMA changed after split!\n\r");
			return -1;
		}
		range_ptr[8192] = 't';
		if (munmap(range_map, 3 * 4096) != 0) {
			outsfmt("Error: Three-page UMAP failed!\n\r");
			return -1;
		}
		if (((char*)reused_map)[0] != 'W' ||
			((char*)independent_map)[0] != 's' ||
			munmap(reused_map, 4096) != 0 ||
			munmap(independent_map, 4096) != 0) {
			outsfmt("Error: Independent mapping changed after UMAP!\n\r");
			return -1;
		}
		fd = sysopen(test_file);
		if (fd < 0 || lseek(fd, 8192, SEEK_SET) != 8192) {
			outsfmt("Error: Failed to seek to third file page!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		char third_verify[sizeof(third_page)] = {};
		int third_read = read(fd, third_verify, sizeof(third_page) - 1);
		close(fd);
		if (third_read != sizeof(third_page) - 1 || third_verify[0] != 't') {
			outsfmt("Error: Right VMA writeback failed!\n\r");
			return -1;
		}
		for (stduint i = 1; i < sizeof(third_page) - 1; i++) {
			if (third_verify[i] != third_page[i]) {
				outsfmt("Error: Right VMA writeback contents mismatch!\n\r");
				return -1;
			}
		}
		outsfmt("Success: Partial UMAP preserved both sides and a freed address was reused!\n\r");

		// 10. A read-only file mapping must remain readable and reject kernel/user writes.
		fd = sysopen(test_file);
		if (fd < 0) return -1;
		void* readonly_map = mmap(nullptr, 4096, PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if (readonly_map == MAP_FAILED) {
			outsfmt("Error: Read-only MMAP failed!\n\r");
			return -1;
		}
		volatile char* readonly_ptr = (volatile char*)readonly_map;
		for (stduint i = 0; i < 24; i++) {
			if (readonly_ptr[i] != expected_content[i]) {
				outsfmt("Error: Read-only MMAP contents mismatch!\n\r");
				return -1;
			}
		}
		fd = sysopen(test_file);
		if (fd < 0 || lseek(fd, 4096, SEEK_SET) != 4096) {
			outsfmt("Error: Failed to prepare read-only buffer test!\n\r");
			return -1;
		}
		int readonly_write = read(fd, (void*)readonly_map, 1);
		close(fd);
		if (readonly_write != 0 || readonly_ptr[0] != 'W') {
			outsfmt("Error: File read wrote through a read-only mapping!\n\r");
			return -1;
		}
		outsfmt("Success: Read-only MMAP is readable and rejected a kernel write!\n\r");
		if (fault_mode) {
			outsfmt("Step 10 fault check: writing to read-only MMAP; SIGSEGV is expected.\n\r");
			readonly_ptr[0] = 'X';
			outsfmt("Error: Direct write to read-only MMAP unexpectedly succeeded!\n\r");
			return -1;
		}
		if (munmap(readonly_map, 4096) != 0) {
			outsfmt("Error: Read-only UMAP failed!\n\r");
			return -1;
		}

		// 11. Repeated mapping and unmapping must not leak VMA state or file references.
		for (int iteration = 0; iteration < 32; iteration++) {
			fd = sysopen(test_file);
			if (fd < 0) return -1;
			void* repeated_map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
			close(fd);
			if (repeated_map == MAP_FAILED || ((char*)repeated_map)[0] != 'W' ||
				munmap(repeated_map, 4096) != 0) {
				outsfmt("Error: Repeated MMAP/UMAP failed at iteration %d!\n\r",
					iteration);
				return -1;
			}
		}
		outsfmt("Success: Repeated MMAP/UMAP completed 32 times.\n\r");

		// 12. A child exit must release an outstanding mapping and write it back.
		pid_t child = fork();
		if (child < 0) {
			outsfmt("Error: Fork for exit cleanup test failed!\n\r");
			return -1;
		}
		if (!child) {
			int child_fd = sysopen(test_file);
			void* child_map = child_fd < 0 ? MAP_FAILED :
				mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
					MAP_SHARED, child_fd, 8192);
			if (child_fd >= 0) close(child_fd);
			if (child_map == MAP_FAILED) _exit(2);
			((char*)child_map)[1] = 'X';
			_exit(0);
		}
		int child_status = -1;
		if (wait(&child_status) != child || child_status != 0) {
			outsfmt("Error: Exit cleanup child failed with status %d!\n\r",
				child_status);
			return -1;
		}
		fd = sysopen(test_file);
		if (fd < 0 || lseek(fd, 8192, SEEK_SET) != 8192) return -1;
		char lifecycle_verify[3] = {};
		int lifecycle_read = read(fd, lifecycle_verify, sizeof(lifecycle_verify));
		close(fd);
		if (lifecycle_read != sizeof(lifecycle_verify) ||
			lifecycle_verify[0] != 't' || lifecycle_verify[1] != 'X') {
			outsfmt("Error: Process exit did not write back its mapping!\n\r");
			return -1;
		}
		outsfmt("Success: Process exit released and wrote back its mapping.\n\r");

		// 13. A forked read-only VMA must survive after the parent unmaps its reference.
		fd = sysopen(test_file);
		if (fd < 0) return -1;
		void* shared_readonly_map = mmap(nullptr, 4096, PROT_READ,
			MAP_SHARED, fd, 0);
		close(fd);
		if (shared_readonly_map == MAP_FAILED ||
			((volatile char*)shared_readonly_map)[0] != 'W') {
			outsfmt("Error: Shared read-only MMAP setup failed!\n\r");
			return -1;
		}
		int share_pipe[2];
		if (pipe(share_pipe) != 0) {
			outsfmt("Error: Pipe for managed-page test failed!\n\r");
			munmap(shared_readonly_map, 4096);
			return -1;
		}
		child = fork();
		if (child < 0) {
			outsfmt("Error: Fork for managed-page test failed!\n\r");
			close(share_pipe[0]);
			close(share_pipe[1]);
			munmap(shared_readonly_map, 4096);
			return -1;
		}
		if (!child) {
			close(share_pipe[1]);
			char token = 0;
			if (read(share_pipe[0], &token, 1) != 1 || token != 'R') _exit(5);
			close(share_pipe[0]);
			if (((volatile char*)shared_readonly_map)[0] != 'W') _exit(6);
			if (munmap(shared_readonly_map, 4096) != 0) _exit(7);
			_exit(0);
		}
		close(share_pipe[0]);
		if (munmap(shared_readonly_map, 4096) != 0) {
			outsfmt("Error: Parent UMAP for managed-page test failed!\n\r");
			close(share_pipe[1]);
			wait(&child_status);
			return -1;
		}
		char share_token = 'R';
		if (write(share_pipe[1], &share_token, 1) != 1) {
			outsfmt("Error: Failed to wake managed-page test child!\n\r");
			close(share_pipe[1]);
			wait(&child_status);
			return -1;
		}
		close(share_pipe[1]);
		child_status = -1;
		if (wait(&child_status) != child || child_status != 0) {
			outsfmt("Error: Managed-page child failed with status %d!\n\r",
				child_status);
			return -1;
		}
		outsfmt("Success: Fork shared a read-only managed page and preserved the child mapping.\n\r");

		// 14. A writable anonymous page must separate on write after fork.
		volatile char* cow_data = (volatile char*)malloc(16);
		if (!cow_data) {
			outsfmt("Error: Failed to allocate anonymous COW test data!\n\r");
			return -1;
		}
		cow_data[0] = 'P';
		int child_ready_pipe[2];
		int parent_ready_pipe[2];
		if (pipe(child_ready_pipe) != 0) {
			outsfmt("Error: Pipe for anonymous COW test failed!\n\r");
			free((void*)cow_data);
			return -1;
		}
		if (pipe(parent_ready_pipe) != 0) {
			outsfmt("Error: Pipe for anonymous COW test failed!\n\r");
			close(child_ready_pipe[0]);
			close(child_ready_pipe[1]);
			free((void*)cow_data);
			return -1;
		}
		child = fork();
		if (child < 0) {
			outsfmt("Error: Fork for anonymous COW test failed!\n\r");
			close(child_ready_pipe[0]);
			close(child_ready_pipe[1]);
			close(parent_ready_pipe[0]);
			close(parent_ready_pipe[1]);
			free((void*)cow_data);
			return -1;
		}
		if (!child) {
			close(child_ready_pipe[0]);
			close(parent_ready_pipe[1]);
			if (cow_data[0] != 'P') _exit(8);
			char token = 'K';
			if (write(child_ready_pipe[1], &token, 1) != 1) _exit(9);
			token = 0;
			if (read(parent_ready_pipe[0], &token, 1) != 1 || token != 'P') _exit(10);
			if (cow_data[0] != 'P') _exit(11);
			cow_data[0] = 'C';
			if (cow_data[0] != 'C') _exit(12);
			close(child_ready_pipe[1]);
			close(parent_ready_pipe[0]);
			_exit(0);
		}
		close(child_ready_pipe[1]);
		close(parent_ready_pipe[0]);
		bool cow_parent_valid = cow_data[0] == 'P' &&
			read(child_ready_pipe[0], (void*)cow_data, 1) == 1 &&
			cow_data[0] == 'K';
		if (cow_parent_valid) {
			cow_data[0] = 'M';
			char cow_token = 'P';
			cow_parent_valid = write(parent_ready_pipe[1], &cow_token, 1) == 1;
		}
		close(child_ready_pipe[0]);
		close(parent_ready_pipe[1]);
		child_status = -1;
		if (wait(&child_status) != child || child_status != 0 ||
			!cow_parent_valid || cow_data[0] != 'M') {
			outsfmt("Error: Anonymous COW test failed with child status %d!\n\r",
				child_status);
			free((void*)cow_data);
			return -1;
		}
		free((void*)cow_data);
		outsfmt("Success: Kernel writes resolved anonymous COW before copying.\n\r");
		outsfmt("Success: Anonymous fork COW separated parent and child writes.\n\r");

		// 15. Exec must release the old image mappings before installing the new image.
		child = fork();
		if (child < 0) {
			outsfmt("Error: Fork for exec cleanup test failed!\n\r");
			return -1;
		}
		if (!child) {
			int child_fd = sysopen(test_file);
			void* child_map = child_fd < 0 ? MAP_FAILED :
				mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
					MAP_SHARED, child_fd, 8192);
			if (child_fd >= 0) close(child_fd);
			if (child_map == MAP_FAILED) _exit(3);
			((char*)child_map)[2] = 'Y';
			const char* vroot = getenv("VRPATH");
			char exec_path[256];
			int exec_path_length = vroot && *vroot ?
				snprintf(exec_path, sizeof(exec_path), "%s/apps/test_mmapfile", vroot) : -1;
			if (exec_path_length < 0 || (stduint)exec_path_length >= sizeof(exec_path)) {
				outsfmt("Error: Cannot resolve test_mmapfile through VRPATH!\n\r");
				_exit(4);
			}
			char* exec_argv[] = { (char*)"test_mmapfile",
				(char*)"exec-child", nullptr };
			int exec_result = execv(exec_path, exec_argv);
			outsfmt("Error: execv(%s) returned %d!\n\r", exec_path, exec_result);
			_exit(4);
		}
		child_status = -1;
		if (wait(&child_status) != child || child_status != 0) {
			outsfmt("Error: Exec cleanup child failed with status %d!\n\r",
				child_status);
			return -1;
		}
		fd = sysopen(test_file);
		if (fd < 0 || lseek(fd, 8192, SEEK_SET) != 8192) return -1;
		lifecycle_read = read(fd, lifecycle_verify, sizeof(lifecycle_verify));
		close(fd);
		if (lifecycle_read != sizeof(lifecycle_verify) ||
			lifecycle_verify[0] != 't' || lifecycle_verify[1] != 'X' ||
			lifecycle_verify[2] != 'Y') {
			outsfmt("Error: Exec did not write back its old mapping!\n\r");
			return -1;
		}
		outsfmt("Success: Exec released and wrote back its old mapping.\n\r");

		// 15a. Cached reads must handle a page boundary and a later file version.
		fd = sysopen(test_file);
		char cache_boundary[4] = {};
		if (fd < 0 || lseek(fd, 4094, SEEK_SET) != 4094 ||
			read(fd, cache_boundary, sizeof(cache_boundary)) != sizeof(cache_boundary) ||
			cache_boundary[0] != 0 || cache_boundary[1] != 0 ||
			cache_boundary[2] != 's' || cache_boundary[3] != 'e' ||
			lseek(fd, 4094, SEEK_SET) != 4094 ||
			read(fd, cache_boundary, sizeof(cache_boundary)) != sizeof(cache_boundary) ||
			cache_boundary[2] != 's') {
			outsfmt("Error: Cached read across a page boundary failed!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		close(fd);
		fd = open(test_file, O_WRONLY);
		char changed_byte = 'C';
		if (fd < 0 || lseek(fd, 0, SEEK_SET) != 0 ||
			write(fd, &changed_byte, 1) != 1) {
			outsfmt("Error: Failed to change a cached file version!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		close(fd);
		fd = sysopen(test_file);
		char updated_byte = 0;
		if (fd < 0 || read(fd, &updated_byte, 1) != 1 || updated_byte != 'C') {
			outsfmt("Error: File write reused an old cached page!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		close(fd);
		outsfmt("Success: Cached reads crossed a page and observed a new file version.\n\r");

		// 16. O_TRUNC must update the backend file and the shared inode size.
		fd = open(test_file, O_WRONLY | O_TRUNC);
		if (fd < 0) {
			outsfmt("Error: O_TRUNC open failed!\n\r");
			return -1;
		}
		close(fd);
		fd = sysopen(test_file);
		char truncated_byte = 0;
		int truncated_size = fd < 0 ? -1 : lseek(fd, 0, SEEK_END);
		int truncated_read = fd < 0 ? -1 : read(fd, &truncated_byte, 1);
		if (fd >= 0) close(fd);
		if (truncated_size != 0 || truncated_read != 0) {
			outsfmt("Error: O_TRUNC did not produce an empty file!\n\r");
			return -1;
		}
		outsfmt("Success: O_TRUNC updated the backend file and inode size.\n\r");

		// 17. Unlink must detach the name while open descriptors and mappings retain the old file.
		rostr unlink_file = "/md0/mmap_u.txt";
		sys_removefil(unlink_file);
		int old_fd = sys_createfil(unlink_file);
		static const char old_content[] = "old mapping";
		static const char new_content[] = "new file";
		if (old_fd < 0 || write(old_fd, old_content, sizeof(old_content)) !=
			sizeof(old_content)) {
			outsfmt("Error: Failed to prepare unlink lifecycle file!\n\r");
			if (old_fd >= 0) close(old_fd);
			return -1;
		}
		void* old_map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
			MAP_SHARED, old_fd, 0);
		if (old_map == MAP_FAILED || ((char*)old_map)[0] != old_content[0]) {
			outsfmt("Error: Failed to map unlink lifecycle file!\n\r");
			if (old_map != MAP_FAILED) munmap(old_map, 4096);
			close(old_fd);
			return -1;
		}
		int unlink_result = unlink(unlink_file);
		int missing_fd = unlink_result == 0 ? sysopen(unlink_file) : -1;
		if (missing_fd >= 0) close(missing_fd);
		if (unlink_result != 0 || missing_fd >= 0) {
			outsfmt("Error: Unlink did not remove the old namespace entry!\n\r");
			munmap(old_map, 4096);
			close(old_fd);
			return -1;
		}
		char old_verify[sizeof(old_content)] = {};
		if (lseek(old_fd, 0, SEEK_SET) != 0 ||
			read(old_fd, old_verify, sizeof(old_verify)) != sizeof(old_verify) ||
			memcmp(old_verify, old_content, sizeof(old_content))) {
			outsfmt("Error: Open descriptor did not retain the unlinked file!\n\r");
			munmap(old_map, 4096);
			close(old_fd);
			return -1;
		}
		int new_fd = sys_createfil(unlink_file);
		if (new_fd < 0 || write(new_fd, new_content, sizeof(new_content)) !=
			sizeof(new_content)) {
			outsfmt("Error: Failed to create a new file under the unlinked name!\n\r");
			if (new_fd >= 0) close(new_fd);
			munmap(old_map, 4096);
			close(old_fd);
			return -1;
		}
		close(new_fd);
		((char*)old_map)[0] = 'O';
		close(old_fd);
		if (munmap(old_map, 4096) != 0) {
			outsfmt("Error: Failed to release the unlinked mapping!\n\r");
			return -1;
		}
		new_fd = sysopen(unlink_file);
		char new_verify[sizeof(new_content)] = {};
		if (new_fd < 0 || read(new_fd, new_verify, sizeof(new_verify)) !=
			sizeof(new_verify) || memcmp(new_verify, new_content,
				sizeof(new_content))) {
			outsfmt("Error: Old mapping changed the replacement file!\n\r");
			if (new_fd >= 0) close(new_fd);
			return -1;
		}
		close(new_fd);
		sys_removefil(unlink_file);
		outsfmt("Success: Unlink retained the old file and isolated its replacement.\n\r");

		// 18. Independent processes must reuse one cached page for the same file.
		rostr cache_file = "/md0/mmap_c.txt";
		sys_removefil(cache_file);
		fd = sys_createfil(cache_file);
		static const char cache_content[] = "shared cache page";
		if (fd < 0 || write(fd, cache_content, sizeof(cache_content) - 1) !=
			sizeof(cache_content) - 1) {
			outsfmt("Error: Failed to prepare cache concurrency file!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		close(fd);
		int cache_gate[2];
		if (pipe(cache_gate) != 0) {
			outsfmt("Error: Failed to create cache concurrency gate!\n\r");
			unlink(cache_file);
			return -1;
		}
		pid_t cache_children[2] = { -1, -1 };
		for (int index = 0; index < 2; index++) {
			cache_children[index] = fork();
			if (cache_children[index] < 0) {
				outsfmt("Error: Failed to fork cache reader %d!\n\r", index);
				close(cache_gate[0]);
				close(cache_gate[1]);
				for (int previous = 0; previous < index; previous++) wait(&child_status);
				unlink(cache_file);
				return -1;
			}
			if (!cache_children[index]) {
				close(cache_gate[1]);
				char token = 0;
				if (read(cache_gate[0], &token, 1) != 1 || token != 'C') _exit(20);
				close(cache_gate[0]);
				int cache_fd = sysopen(cache_file);
				char observed[sizeof(cache_content)] = {};
				int cache_read = cache_fd < 0 ? -1 :
					read(cache_fd, observed, sizeof(cache_content) - 1);
				if (cache_fd >= 0) close(cache_fd);
				_exit(cache_read == sizeof(cache_content) - 1 &&
					!memcmp(observed, cache_content, sizeof(cache_content) - 1) ? 0 : 21);
			}
		}
		close(cache_gate[0]);
		const char cache_tokens[2] = { 'C', 'C' };
		int gate_written = write(cache_gate[1], cache_tokens, sizeof(cache_tokens));
		close(cache_gate[1]);
		if (gate_written != sizeof(cache_tokens)) {
			outsfmt("Error: Failed to release cache readers!\n\r");
			wait(&child_status);
			wait(&child_status);
			unlink(cache_file);
			return -1;
		}
		bool cache_children_ok = true;
		for (int index = 0; index < 2; index++) {
			child_status = -1;
			pid_t completed = wait(&child_status);
			if ((completed != cache_children[0] && completed != cache_children[1]) ||
				child_status != 0) {
				outsfmt("Error: Cache reader exited with status %d!\n\r", child_status);
				cache_children_ok = false;
			}
		}
		if (!cache_children_ok) {
			unlink(cache_file);
			return -1;
		}
		fd = sysopen(cache_file);
		char cache_verify[sizeof(cache_content)] = {};
		int cache_read = fd < 0 ? -1 :
			read(fd, cache_verify, sizeof(cache_content) - 1);
		if (fd >= 0) close(fd);
		if (cache_read != sizeof(cache_content) - 1 ||
			memcmp(cache_verify, cache_content, sizeof(cache_content) - 1) ||
			unlink(cache_file) != 0) {
			outsfmt("Error: Cache reuse or cleanup failed!\n\r");
			return -1;
		}
		outsfmt("Success: Independent processes read the same cached file page.\n\r");

		// 19. Two independent mappings must retain the same read-only file page.
		rostr map_file = "/md0/mmap_v.txt";
		const stduint map_page_size = 4096;
		sys_removefil(map_file);
		fd = sys_createfil(map_file);
		static const char map_content[] = "read-only cached page";
		if (fd < 0 || write(fd, map_content, sizeof(map_content) - 1) !=
			sizeof(map_content) - 1) {
			outsfmt("Error: Failed to prepare shared mapping file!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		close(fd);
		int map_ready_pipe[2] = { -1, -1 };
		int map_release[2][2] = { { -1, -1 }, { -1, -1 } };
		if (pipe(map_ready_pipe) != 0 || pipe(map_release[0]) != 0 ||
			pipe(map_release[1]) != 0) {
			outsfmt("Error: Failed to create shared mapping gates!\n\r");
			if (map_ready_pipe[0] >= 0) close(map_ready_pipe[0]);
			if (map_ready_pipe[1] >= 0) close(map_ready_pipe[1]);
			for (int index = 0; index < 2; index++) {
				if (map_release[index][0] >= 0) close(map_release[index][0]);
				if (map_release[index][1] >= 0) close(map_release[index][1]);
			}
			unlink(map_file);
			return -1;
		}
		pid_t map_children[2] = { -1, -1 };
		for (int index = 0; index < 2; index++) {
			map_children[index] = fork();
			if (map_children[index] < 0) {
				outsfmt("Error: Failed to fork shared mapping child %d!\n\r", index);
				close(map_ready_pipe[0]);
				close(map_ready_pipe[1]);
				for (int gate = 0; gate < 2; gate++) {
					close(map_release[gate][0]);
					close(map_release[gate][1]);
				}
				for (int previous = 0; previous < index; previous++) wait(&child_status);
				unlink(map_file);
				return -1;
			}
			if (!map_children[index]) {
				close(map_ready_pipe[0]);
				for (int gate = 0; gate < 2; gate++) {
					close(map_release[gate][1]);
					if (gate != index) close(map_release[gate][0]);
				}
				int map_fd = sysopen(map_file);
				void* mapped = map_fd < 0 ? MAP_FAILED :
					mmap(nullptr, map_page_size * 2, PROT_READ, MAP_SHARED, map_fd, 0);
				if (map_fd >= 0) close(map_fd);
				if (mapped == MAP_FAILED) _exit(30);
				const char* bytes = (const char*)mapped;
				if (memcmp(bytes, map_content, sizeof(map_content) - 1) ||
					bytes[sizeof(map_content) - 1] != 0 ||
					bytes[map_page_size - 1] != 0 || bytes[map_page_size] != 0 ||
					bytes[map_page_size * 2 - 1] != 0) _exit(31);
				const char ready = 'R';
				if (write(map_ready_pipe[1], &ready, 1) != 1) _exit(32);
				close(map_ready_pipe[1]);
				char release = 0;
				if (read(map_release[index][0], &release, 1) != 1 ||
					release != 'G') _exit(33);
				close(map_release[index][0]);
				if (bytes[map_page_size] != 0 ||
					(bytes[0] != map_content[0] && bytes[0] != 'U') ||
					munmap(mapped, map_page_size * 2) != 0) _exit(34);
				_exit(0);
			}
		}
		close(map_ready_pipe[1]);
		close(map_release[0][0]);
		close(map_release[1][0]);
		bool map_ready = true;
		for (int index = 0; index < 2; index++) {
			char ready = 0;
			if (read(map_ready_pipe[0], &ready, 1) != 1 || ready != 'R')
				map_ready = false;
		}
		close(map_ready_pipe[0]);
		outsfmt("Stage 10 map children: pid=%d and pid=%d.\n\r",
			map_children[0], map_children[1]);
		bool new_version_ok = false;
		if (map_ready) {
			fd = open(map_file, O_WRONLY);
			const char updated = 'U';
			bool written = fd >= 0 && write(fd, &updated, 1) == 1;
			if (fd >= 0) close(fd);
			fd = written ? sysopen(map_file) : -1;
			void* new_map = fd < 0 ? MAP_FAILED :
				mmap(nullptr, map_page_size, PROT_READ, MAP_SHARED, fd, 0);
			if (fd >= 0) close(fd);
			new_version_ok = new_map != MAP_FAILED &&
				((const char*)new_map)[0] == updated;
			if (new_map != MAP_FAILED)
				new_version_ok = munmap(new_map, map_page_size) == 0 &&
					new_version_ok;
		}
		const char release = 'G';
		bool first_released = map_ready &&
			write(map_release[0][1], &release, 1) == 1;
		close(map_release[0][1]);
		child_status = -1;
		pid_t first_completed = wait(&child_status);
		bool first_ok = first_completed == map_children[0] && child_status == 0;
		bool second_released = map_ready &&
			write(map_release[1][1], &release, 1) == 1;
		close(map_release[1][1]);
		child_status = -1;
		pid_t second_completed = wait(&child_status);
		bool second_ok = second_completed == map_children[1] && child_status == 0;
		// 20. Private file mappings must isolate user writes, kernel writes and fork.
		fd = sysopen(map_file);
		void* private_read_map = fd < 0 ? MAP_FAILED :
			mmap(nullptr, map_page_size, PROT_READ, MAP_PRIVATE, fd, 0);
		void* private_write_map = fd < 0 ? MAP_FAILED :
			mmap(nullptr, map_page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		void* private_fork_map = fd < 0 ? MAP_FAILED :
			mmap(nullptr, map_page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		void* private_kernel_map = fd < 0 ? MAP_FAILED :
			mmap(nullptr, map_page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (fd >= 0) close(fd);
		if (private_read_map == MAP_FAILED || private_write_map == MAP_FAILED ||
			private_fork_map == MAP_FAILED || private_kernel_map == MAP_FAILED ||
			((const char*)private_read_map)[0] != 'U' ||
			((const char*)private_write_map)[0] != 'U' ||
			((const char*)private_fork_map)[0] != 'U' ||
			((const char*)private_read_map)[sizeof(map_content) - 1] != 0) {
			outsfmt("Error: MAP_PRIVATE setup failed!\n\r");
			return -1;
		}
		((char*)private_write_map)[0] = 'P';
		if (((const char*)private_write_map)[0] != 'P' ||
			((const char*)private_fork_map)[0] != 'U' ||
			((const char*)private_read_map)[0] != 'U') {
			outsfmt("Error: User write changed another file mapping!\n\r");
			return -1;
		}
		int private_pipe[2] = { -1, -1 };
		const char kernel_marker = 'K';
		if (pipe(private_pipe) != 0 ||
			write(private_pipe[1], &kernel_marker, 1) != 1 ||
			read(private_pipe[0], private_kernel_map, 1) != 1) {
			outsfmt("Error: Kernel write into MAP_PRIVATE failed!\n\r");
			return -1;
		}
		close(private_pipe[0]);
		close(private_pipe[1]);
		if (((const char*)private_kernel_map)[0] != 'K' ||
			((const char*)private_read_map)[0] != 'U' ||
			((const char*)private_fork_map)[0] != 'U') {
			outsfmt("Error: Kernel write changed another file mapping!\n\r");
			return -1;
		}
		pid_t private_child = fork();
		if (private_child < 0) {
			outsfmt("Error: MAP_PRIVATE fork failed!\n\r");
			return -1;
		}
		if (!private_child) {
			if (((const char*)private_write_map)[0] != 'P' ||
				((const char*)private_fork_map)[0] != 'U' ||
				((const char*)private_kernel_map)[0] != 'K') _exit(40);
			((char*)private_write_map)[0] = 'C';
			((char*)private_fork_map)[0] = 'D';
			((char*)private_kernel_map)[0] = 'E';
			if (((const char*)private_write_map)[0] != 'C' ||
				((const char*)private_fork_map)[0] != 'D' ||
				((const char*)private_kernel_map)[0] != 'E' ||
				((const char*)private_read_map)[0] != 'U') _exit(41);
			_exit(0);
		}
		int private_child_status = -1;
		pid_t private_completed = wait(&private_child_status);
		if (private_completed != private_child || private_child_status != 0 ||
			((const char*)private_write_map)[0] != 'P' ||
			((const char*)private_fork_map)[0] != 'U' ||
			((const char*)private_kernel_map)[0] != 'K' ||
			((const char*)private_read_map)[0] != 'U') {
			outsfmt("Error: MAP_PRIVATE fork changed the parent mapping!\n\r");
			return -1;
		}

		((char*)private_write_map)[0] = 'Q';
		((char*)private_fork_map)[0] = 'R';
		if (((const char*)private_write_map)[0] != 'Q' ||
			((const char*)private_fork_map)[0] != 'R' ||
			((const char*)private_read_map)[0] != 'U') {
			outsfmt("Error: Parent MAP_PRIVATE write after fork failed!\n\r");
			return -1;
		}
		bool private_unmapped = munmap(private_read_map, map_page_size) == 0;
		private_unmapped = munmap(private_write_map, map_page_size) == 0 &&
			private_unmapped;
		private_unmapped = munmap(private_fork_map, map_page_size) == 0 &&
			private_unmapped;
		private_unmapped = munmap(private_kernel_map, map_page_size) == 0 &&
			private_unmapped;
		fd = sysopen(map_file);
		char private_disk_byte = 0;
		bool private_disk_ok = fd >= 0 &&
			read(fd, &private_disk_byte, 1) == 1 && private_disk_byte == 'U';
		if (fd >= 0) close(fd);
		if (!private_unmapped || !private_disk_ok) {
			outsfmt("Error: MAP_PRIVATE unmap wrote back to the file!\n\r");
			return -1;
		}
		bool map_removed = unlink(map_file) == 0;
		if (!map_ready || !new_version_ok || !first_released || !first_ok ||
			!second_released || !second_ok || !map_removed) {
			outsfmt("Error: Independent cached mappings failed!\n\r");
			return -1;
		}
		outsfmt("Success: Independent read-only mappings survived version and unmap changes.\n\r");
		outsfmt("Success: Read-only MAP_PRIVATE read cached content.\n\r");
		outsfmt("Success: MAP_PRIVATE isolated user writes, kernel writes, fork and unmap.\n\r");

		// 21. Mutable shared pages must agree across aliases, fork and regular I/O.
		rostr shared_file = "/md0/mmap_s.txt";
		sys_removefil(shared_file);
		fd = sys_createfil(shared_file);
		static const char shared_initial[] = "abcd";
		if (fd < 0 || write(fd, shared_initial, 4) != 4) {
			outsfmt("Error: Failed to prepare mutable shared file!\n\r");
			if (fd >= 0) close(fd);
			return -1;
		}
		void* shared_writer = mmap(nullptr, map_page_size,
			PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		void* shared_reader = mmap(nullptr, map_page_size,
			PROT_READ, MAP_SHARED, fd, 0);
		void* private_snapshot = mmap(nullptr, map_page_size,
			PROT_READ, MAP_PRIVATE, fd, 0);
		if (shared_writer == MAP_FAILED || shared_reader == MAP_FAILED ||
			private_snapshot == MAP_FAILED ||
			((char*)shared_writer)[0] != 'a' ||
			((char*)shared_reader)[0] != 'a' ||
			((char*)private_snapshot)[0] != 'a') {
			outsfmt("Error: Mutable shared mapping setup failed!\n\r");
			return -1;
		}
		((char*)shared_writer)[0] = 'S';
		char shared_readback = 0;
		if (((char*)shared_reader)[0] != 'S' ||
			lseek(fd, 0, SEEK_SET) != 0 ||
			read(fd, &shared_readback, 1) != 1 || shared_readback != 'S' ||
			((char*)private_snapshot)[0] != 'a') {
			outsfmt("Error: Shared user write was not visible to read()!\n\r");
			return -1;
		}
		void* private_after_write = mmap(nullptr, map_page_size,
			PROT_READ, MAP_PRIVATE, fd, 0);
		if (private_after_write == MAP_FAILED ||
			((char*)private_after_write)[0] != 'S') {
			outsfmt("Error: New private snapshot missed an earlier shared write!\n\r");
			return -1;
		}
		pid_t shared_child = fork();
		if (shared_child < 0) {
			outsfmt("Error: Mutable shared fork failed!\n\r");
			return -1;
		}
		if (!shared_child) {
			if (((char*)shared_reader)[0] != 'S') _exit(50);
			((char*)shared_writer)[1] = 'F';
			_exit(((char*)shared_reader)[1] == 'F' ? 0 : 51);
		}
		int shared_status = -1;
		if (wait(&shared_status) != shared_child || shared_status != 0 ||
			((char*)shared_reader)[1] != 'F') {
			outsfmt("Error: Fork did not retain one mutable shared page!\n\r");
			return -1;
		}
		char ordinary_marker = 'W';
		if (lseek(fd, 2, SEEK_SET) != 2 ||
			write(fd, &ordinary_marker, 1) != 1 ||
			((char*)shared_writer)[2] != 'W' ||
			((char*)shared_reader)[2] != 'W' ||
			((char*)private_snapshot)[2] != 'c') {
			outsfmt("Error: Ordinary write did not update shared aliases!\n\r");
			return -1;
		}
		((char*)shared_writer)[16] = 'X'; // A mapped write past EOF does not extend the file.
		ordinary_marker = 'E';
		const int growth_seek = lseek(fd, 20, SEEK_SET);
		const int growth_write = growth_seek == 20 ?
			write(fd, &ordinary_marker, 1) : -1;
		const char growth_gap = growth_write == 1 ?
			((char*)shared_reader)[16] : 0;
		const char growth_marker = growth_write == 1 ?
			((char*)shared_reader)[20] : 0;
		if (growth_seek != 20 || growth_write != 1 ||
			growth_gap != 0 || growth_marker != 'E') {
			outsfmt("Error: File growth mismatch: seek=%d write=%d gap=%d marker=%d!\n\r",
				growth_seek, growth_write, (int)growth_gap, (int)growth_marker);
			return -1;
		}
		int shared_pipe[2] = { -1, -1 };
		ordinary_marker = 'K';
		if (pipe(shared_pipe) != 0 ||
			write(shared_pipe[1], &ordinary_marker, 1) != 1 ||
			read(shared_pipe[0], (char*)shared_writer + 3, 1) != 1) {
			outsfmt("Error: Kernel copy into shared page failed!\n\r");
			return -1;
		}
		close(shared_pipe[0]);
		close(shared_pipe[1]);
		if (((char*)shared_reader)[3] != 'K' ||
			lseek(fd, 3, SEEK_SET) != 3 ||
			read(fd, &shared_readback, 1) != 1 || shared_readback != 'K' ||
			munmap(shared_writer, map_page_size) != 0 ||
			((char*)shared_reader)[0] != 'S') {
			outsfmt("Error: Kernel shared write or partial alias release failed!\n\r");
			return -1;
		}
		int truncate_fd = open(shared_file, O_WRONLY | O_TRUNC);
		if (truncate_fd < 0) {
			outsfmt("Error: Shared-page truncate failed!\n\r");
			return -1;
		}
		ordinary_marker = 'N';
		int new_written = write(truncate_fd, &ordinary_marker, 1);
		close(truncate_fd);
		if (new_written != 1 || ((char*)shared_reader)[0] != 'N' ||
			((char*)shared_reader)[1] != 0 ||
			((char*)private_snapshot)[0] != 'a' ||
			((char*)private_after_write)[0] != 'S' ||
			munmap(shared_reader, map_page_size) != 0 ||
			munmap(private_snapshot, map_page_size) != 0 ||
			munmap(private_after_write, map_page_size) != 0) {
			outsfmt("Error: Truncate or private snapshot consistency failed!\n\r");
			return -1;
		}
		close(fd);
		if (unlink(shared_file) != 0) {
			outsfmt("Error: Mutable shared file cleanup failed!\n\r");
			return -1;
		}
		outsfmt("Success: Shared aliases, fork, read/write, kernel copy and truncate stayed coherent.\n\r");
		outsfmt("=== File MMAP Test ALL PASSED! ===\n\r");

		// 22. Clean up the temporary test file
		sys_removefil(test_file);
		outsfmt("Step 22: Cleaned up test file.\n\r");
	}
	#endif

	// loop sysrest(0, 0);
	return 0;
}

