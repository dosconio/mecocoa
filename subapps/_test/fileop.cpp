//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <c/ISO_IEC_STD/errno.h>
#include <c/ustring.h>

using namespace uni;

// Read through a fresh open to distinguish namespace replacement from the retained old descriptor.
static bool RenameContents(const char* path, const char* expected) {
	const int fd = open(path, O_RDONLY);
	if (fd < 0) return false;
	char buffer[16] = {};
	const int length = int(StrLength(expected));
	const int got = int(read(fd, buffer, sizeof(buffer)));
	close(fd);
	return got == length && !StrCompare(buffer, expected);
}

// Use an exclusively created directory and never reuse or truncate pre-existing test data.
static int TestRename(const char* base) {
	if (!base || StrLength(base) > 400) return 1;
	auto* paths = static_cast<char*>(malloc(8 * 512));
	if (!paths) return 1;
	char* root = paths;
	snprintf(root, 512, "%s/rename-%u", base, unsigned(getpid()));
	if (mkdir(root, 0700)) { free(paths); return 1; }
	static const char* const names[] = { "source-long-name.txt", "target-long-name.txt", "TARGET-LONG-NAME.TXT", "left", "right", "right/child", "right/moved.txt" };
	for (unsigned i = 0; i < 7; ++i) snprintf(paths + (i + 1) * 512, 512, "%s/%s", root, names[i]);
	char* source = paths + 512;
	char* target = paths + 1024;
	char* alias = paths + 1536;
	char* left = paths + 2048;
	char* right = paths + 2560;
	char* child = paths + 3072;
	char* moved = paths + 3584;
	int source_fd = -1, target_fd = -1, alias_fd = -1, child_fd = -1;
	const char* step = "create files";
	bool passed = false;
	do {
		source_fd = open(source, O_CREAT | O_EXCL | O_RDWR, 0600);
		target_fd = open(target, O_CREAT | O_EXCL | O_RDWR, 0600);
		if (source_fd < 0 || target_fd < 0 || write(source_fd, "NEW", 3) != 3) break;
		// FAT accepts this alternate spelling; case-sensitive RootFs must report an absent alias.
		alias_fd = open(alias, O_RDWR);
		if (alias_fd < 0 && errno != ENOENT) break;
		step = "retain an empty target after replacement";
		if (rename(source, target) || !RenameContents(target, "NEW")) break;
		if (write(target_fd, "OLD", 3) != 3 || !RenameContents(target, "NEW")) break;
		if (alias_fd >= 0 && (lseek(alias_fd, 0, SEEK_SET) != 0 || write(alias_fd, "old", 3) != 3 || !RenameContents(target, "NEW"))) break;
		const int absent = open(source, O_RDONLY);
		if (absent >= 0) { close(absent); break; }
		if (errno != ENOENT) break;
		step = "type mismatch and nonempty directory";
		if (mkdir(left, 0700) || mkdir(right, 0700)) break;
		if (rename(target, left) != -1 || errno != EISDIR) break;
		if (rename(left, target) != -1 || errno != ENOTDIR) break;
		child_fd = open(child, O_CREAT | O_EXCL | O_RDWR, 0600);
		if (child_fd < 0 || rename(left, right) != -1 || errno != ENOTEMPTY) break;
		close(child_fd); child_fd = -1;
		if (unlink(child)) break;
		step = "replace an empty directory and move across parents";
		if (rename(left, right) || rename(target, moved) || !RenameContents(moved, "NEW")) break;
		step = "retain an overwritten nonempty file";
		close(source_fd); source_fd = -1;
		source_fd = open(source, O_CREAT | O_EXCL | O_RDWR, 0600);
		if (source_fd < 0 || write(source_fd, "END", 3) != 3) break;
		const int retained = open(moved, O_RDWR);
		if (retained < 0) break;
		const bool replaced = !rename(source, moved) && lseek(retained, 0, SEEK_SET) == 0 && write(retained, "OLD", 3) == 3 && RenameContents(moved, "END");
		close(retained);
		if (!replaced) break;
		passed = true;
	} while (false);
	if (!passed) outsfmt("rename test failed: %s (errno=%d)\n\r", step, errno);
	if (child_fd >= 0) close(child_fd);
	if (alias_fd >= 0) close(alias_fd);
	if (target_fd >= 0) close(target_fd);
	if (source_fd >= 0) close(source_fd);
	unlink(child); unlink(moved); unlink(source); unlink(target);
	rmdir(left); rmdir(right); rmdir(root);
	free(paths);
	if (passed) outsfmt("rename test passed\n\r");
	return passed ? 0 : 1;
}

// mnt33 fat16
// mnt34 fat32

int main(int argc, char** argv)
{
	if (argc == 3 && !StrCompare(argv[1], "--rename")) return TestRename(argv[2]);

	rostr filename = "/md0/owo.txt";

	int fd = sysopen(filename);
	if (fd < 0) {
		fd = sys_createfil(filename);
	}

	if (fd >= 0) {
		outsfmt("Open success! FD=%d\n\r", fd);
		const char* msg = "Hello from Mecocoa VFS!\n";
		write(fd, (void*)msg, 24);
		close(fd);

		fd = sysopen(filename);
		if (fd >= 0) {
			char buf[32] = {0};
			read(fd, buf, 24);
			outsfmt("Read result: %s\n\r", buf);
			close(fd);
		}
	} else {
		outsfmt("Open failed with code %d\n\r", fd);
	}

	sys_removefil(filename);


}

