#include "aaaaa.h"
#include <pwd.h>
#include <grp.h>
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

// Reserve an aligned empty member vector inside caller storage before receiving the group strings.
static int QueryGroup(syscall_user_query_t query, stduint key, struct group* entry, char* buffer, size_t size, struct group** result) {
	if (!result) return EINVAL;
	*result = nullptr;
	if (!entry || (!buffer && size)) return EINVAL;
	const stduint padding = (sizeof(char*) - (_IMM(buffer) % sizeof(char*))) % sizeof(char*);
	const stduint reserved = padding + sizeof(char*);
	if (size < reserved) return ERANGE;
	char** members = reinterpret_cast<char**>(buffer + padding);
	syscall_account_buffer_t output{ buffer + reserved, size - reserved, 0, 0 };
	const int status = QueryAccount(query, key, output);
	if (status) return status == ENOENT ? 0 : status;
	if (output.size > size - reserved) return EIO;
	char* next = output.data;
	const char* end = next + output.size;
	struct group value{};
	value.gr_name = TakeAccountField(next, end);
	value.gr_passwd = TakeAccountField(next, end);
	if (!value.gr_name || !value.gr_passwd || next != end) return EIO;
	members[0] = nullptr;
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
	return QueryGroup(syscall_user_query_t::GroupByName, _IMM(name), entry, buffer, size, result);
}

int getgrgid_r(gid_t gid, struct group* entry, char* buffer, size_t size, struct group** result) {
	return QueryGroup(syscall_user_query_t::GroupByID, stduint(gid), entry, buffer, size, result);
}

int UserLogin(const char* name, const char* password) {
	const auto status = stdsint(syscall(syscall_t::USER, _IMM(syscall_user_query_t::Login), _IMM(name), _IMM(password)));
	if (status >= 0) return int(status);
	errno = int(-status);
	return -1;
}
