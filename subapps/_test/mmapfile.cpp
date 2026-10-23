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
		outsfmt("=== File MMAP Test ALL PASSED! ===\n\r");

		// 18. Clean up the temporary test file
		sys_removefil(test_file);
		outsfmt("Step 18: Cleaned up test file.\n\r");
	}
	#endif

	// loop sysrest(0, 0);
	return 0;
}

