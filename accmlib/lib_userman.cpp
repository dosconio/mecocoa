#include "aaaaa.h"
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <unistd.h>
#include <c/ISO_IEC_STD/errno.h>

// Decode only terminated strings inside the kernel-reported payload length.
static char* TakeAccountField(char*& next, const char* end) {
	char* field = next;
	while (next < end) if (!*next++) return field;
	return nullptr;
}

static int QueryAccount(syscall_user_query_t query, stduint key, syscall_account_buffer_t& output) {
	const auto status = stdsint(syscall(syscall_t::USER, _IMM(query), key, _IMM(&output)));
	return status < 0 ? int(-status) : 0;
}

// POSIX.1 getpwnam_r/getpwuid_r return zero with a null result when the account is absent.
static int QueryUser(syscall_user_query_t query, stduint key, struct passwd* entry, char* buffer, size_t size, struct passwd** result) {
	if (!result) return EINVAL;
	*result = nullptr;
	if (!entry || (!buffer && size)) return EINVAL;
	syscall_account_buffer_t output{ buffer, size, 0, 0 };
	const int status = QueryAccount(query, key, output);
	if (status) return status == ENOENT ? 0 : status;
	if (output.size > size) return EIO;
	char* next = buffer;
	const char* end = buffer + output.size;
	struct passwd value{};
	value.pw_name = TakeAccountField(next, end);
	value.pw_passwd = TakeAccountField(next, end);
	value.pw_gecos = TakeAccountField(next, end);
	value.pw_dir = TakeAccountField(next, end);
	value.pw_shell = TakeAccountField(next, end);
	if (!value.pw_name || !value.pw_passwd || !value.pw_gecos || !value.pw_dir || !value.pw_shell || next != end) return EIO;
	value.pw_uid = uid_t(output.uid);
	value.pw_gid = gid_t(output.gid);
	*entry = value;
	*result = entry;
	return 0;
}

// Place the aligned, null-terminated member pointer array after the received strings in caller storage.
static int QueryGroup(syscall_user_query_t query, stduint key, struct group* entry, char* buffer, size_t size, struct group** result) {
	if (!result) return EINVAL;
	*result = nullptr;
	if (!entry || (!buffer && size)) return EINVAL;
	syscall_account_buffer_t output{ buffer, size, 0, 0 };
	const int status = QueryAccount(query, key, output);
	if (status) return status == ENOENT ? 0 : status;
	if (!buffer || output.size > size) return EIO;
	char* next = buffer;
	const char* end = buffer + output.size;
	struct group value{};
	value.gr_name = TakeAccountField(next, end);
	value.gr_passwd = TakeAccountField(next, end);
	if (!value.gr_name || !value.gr_passwd) return EIO;
	char* first_member = next;
	stduint count = 0;
	while (next < end) {
		if (!TakeAccountField(next, end)) return EIO;
		count++;
	}
	const stduint padding = (sizeof(char*) - (_IMM(end) % sizeof(char*))) % sizeof(char*);
	if (padding > size - output.size) return ERANGE;
	const stduint available = size - output.size - padding;
	if (count >= available / sizeof(char*)) return ERANGE;
	char** members = reinterpret_cast<char**>(buffer + output.size + padding);
	next = first_member;
	for (stduint i = 0; i < count; i++) members[i] = TakeAccountField(next, end);
	members[count] = nullptr;
	value.gr_mem = members;
	value.gr_gid = gid_t(output.gid);
	*entry = value;
	*result = entry;
	return 0;
}

int getpwnam_r(const char* name, struct passwd* entry, char* buffer, size_t size, struct passwd** result) {
	return QueryUser(syscall_user_query_t::UserByName, _IMM(name), entry, buffer, size, result);
}

int getpwuid_r(uid_t uid, struct passwd* entry, char* buffer, size_t size, struct passwd** result) {
	return QueryUser(syscall_user_query_t::UserByID, stduint(uid), entry, buffer, size, result);
}

int getgrnam_r(const char* name, struct group* entry, char* buffer, size_t size, struct group** result) {
	return QueryGroup(syscall_user_query_t::GroupByNameMembers, _IMM(name), entry, buffer, size, result);
}

int getgrgid_r(gid_t gid, struct group* entry, char* buffer, size_t size, struct group** result) {
	return QueryGroup(syscall_user_query_t::GroupByIDMembers, stduint(gid), entry, buffer, size, result);
}

// POSIX.1 getgroups reports the saved list only; a zero size requests its current entry count.
int getgroups(int size, gid_t list[]) {
	if (size < 0) { errno = EINVAL; return -1; }
	const auto status = stdsint(syscall(syscall_t::USER, _IMM(syscall_user_query_t::GetGroups), stduint(size), _IMM(list)));
	if (status >= 0) return int(status);
	errno = int(-status);
	return -1;
}

// Only root may replace supplementary groups, including clearing the list with size zero.
int setgroups(size_t size, const gid_t list[]) {
	const auto status = stdsint(syscall(syscall_t::USER, _IMM(syscall_user_query_t::SetGroups), size, _IMM(list)));
	if (status >= 0) return int(status);
	errno = int(-status);
	return -1;
}

// BSD getgrouplist includes the supplied base GID and reports required capacity through count.
int getgrouplist(const char* user, gid_t group, gid_t list[], int* count) {
	if (!user || !count || *count < 0 || group < 0 || uint64(group) > 0x7FFFFFFFu) { errno = EINVAL; return -1; }
	syscall_group_list_t request{ list, stduint(*count), uint32(group), 0 };
	const auto status = stdsint(syscall(syscall_t::USER, _IMM(syscall_user_query_t::AccountGroups), _IMM(user), _IMM(&request)));
	if (status == 0 || status == -ERANGE) *count = int(request.count);
	if (status >= 0) return int(request.count);
	errno = int(-status);
	return -1;
}

// Build a complete account group list before replacing credentials; allocation or lookup failure changes nothing.
int initgroups(const char* user, gid_t group) {
	int count = 0;
	if (getgrouplist(user, group, nullptr, &count) < 0 && errno != ERANGE) return -1;
	for (;;) {
		if (stduint(count) > ~stduint(0) / sizeof(gid_t)) { errno = EOVERFLOW; return -1; }
		auto* list = static_cast<gid_t*>(malloc(stduint(count) * sizeof(gid_t)));
		if (!list) { errno = ENOMEM; return -1; }
		int capacity = count;
		if (getgrouplist(user, group, list, &capacity) >= 0) {
			const int status = setgroups(size_t(capacity), list);
			const int saved_errno = errno;
			free(list);
			errno = saved_errno;
			return status;
		}
		const int saved_errno = errno;
		free(list);
		if (saved_errno != ERANGE || capacity <= count) { errno = saved_errno; return -1; }
		count = capacity;
	}
}

int UserLogin(const char* name, const char* password) {
	const auto status = stdsint(syscall(syscall_t::USER, _IMM(syscall_user_query_t::Login), _IMM(name), _IMM(password)));
	if (status >= 0) return int(status);
	errno = int(-status);
	return -1;
}
