#include "aaaaa.h"
#include <c/ISO_IEC_STD/errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>

namespace {
	typedef void (*ExitFunction)(void);
	typedef void (*CxaExitFunction)(void*);

	struct ExitCallback {
		ExitCallback* next;
		union {
			ExitFunction plain;
			CxaExitFunction cxa;
		} function;
		void* argument;
		void* dso_handle;
		bool plain_function;
	};

	ExitCallback* exit_callbacks = nullptr;

	ExitCallback* AllocateExitCallback() {
		return (ExitCallback*)malloc(sizeof(ExitCallback));
	}

	int RegisterCxaExitCallback(CxaExitFunction function, void* argument,
		void* dso_handle) {
		if (!function) return -1;
		ExitCallback* callback = AllocateExitCallback();
		if (!callback) return -1;
		callback->function.cxa = function;
		callback->argument = argument;
		callback->dso_handle = dso_handle;
		callback->plain_function = false;
		callback->next = exit_callbacks;
		exit_callbacks = callback;
		return 0;
	}

	int RegisterPlainExitCallback(ExitFunction function) {
		if (!function) return -1;
		ExitCallback* callback = AllocateExitCallback();
		if (!callback) return -1;
		callback->function.plain = function;
		callback->argument = nullptr;
		callback->dso_handle = nullptr;
		callback->plain_function = true;
		callback->next = exit_callbacks;
		exit_callbacks = callback;
		return 0;
	}
}

// Itanium C++ ABI 3.3.5: register a destructor for one DSO object.
extern "C" int __cxa_atexit(CxaExitFunction function, void* argument,
	void* dso_handle) {
	return RegisterCxaExitCallback(function, argument, dso_handle);
}

// Itanium C++ ABI 3.3.5: finalize matching callbacks in reverse registration order.
extern "C" void __cxa_finalize(void* dso_handle) {
	while (true) {
		ExitCallback** link = &exit_callbacks;
		while (*link && dso_handle && (*link)->dso_handle != dso_handle)
			link = &(*link)->next;
		if (!*link) return;

		ExitCallback* callback = *link;
		*link = callback->next;
		const bool plain_function = callback->plain_function;
		if (plain_function) {
			const ExitFunction function = callback->function.plain;
			free(callback);
			function();
		}
		else {
			const CxaExitFunction function = callback->function.cxa;
			void* const argument = callback->argument;
			free(callback);
			function(argument);
		}
	}
}

extern "C" int atexit(ExitFunction function) {
	return RegisterPlainExitCallback(function);
}

extern "C" void _exit(int code)
{
	syscall(syscall_t::EXIT, code, nil, nil);// 戰至最後一刻！
	*((volatile byte*)nullptr) = nil;// 自刎歸天⚔️（介錯） __builtin_trap()
	while (1);// 歸天失敗
}

extern "C" void exit(int code)
{
	__cxa_finalize(nullptr);
	_exit(code);
}

pid_t fork() {
	return syscall(syscall_t::FORK, nil, nil, nil);
}

static stdsint ProcessIdResult(stduint result) {
	const auto status = stdsint(result);
	if (status >= 0) return status;
	errno = int(-status);
	return -1;
}

pid_t getpid(void) {
	return ProcessIdResult(syscall(syscall_t::GETI, _IMM(syscall_id_type_t::Process)));
}

uid_t getuid(void) {
	return ProcessIdResult(syscall(syscall_t::GETI, _IMM(syscall_id_type_t::User)));
}

uid_t geteuid(void) {
	return ProcessIdResult(syscall(syscall_t::GETI, _IMM(syscall_id_type_t::EffectiveUser)));
}

gid_t getgid(void) {
	return ProcessIdResult(syscall(syscall_t::GETI, _IMM(syscall_id_type_t::UserGroup)));
}

gid_t getegid(void) {
	return ProcessIdResult(syscall(syscall_t::GETI, _IMM(syscall_id_type_t::EffectiveUserGroup)));
}

int setuid(uid_t uid) {
	return int(ProcessIdResult(syscall(syscall_t::SETI, _IMM(syscall_id_type_t::User), stduint(uid))));
}

int setgid(gid_t gid) {
	return int(ProcessIdResult(syscall(syscall_t::SETI, _IMM(syscall_id_type_t::UserGroup), stduint(gid))));
}

//

pid_t wait(int* status)
{
	return syscall(syscall_t::WAIT, 0, _IMM(status), nil);
}

int waitid(idtype_t idtype, id_t id, siginfo_t* infop, int options)
{
	stduint target_pid = (idtype == P_PID) ? id : 0;
	int status;
	pid_t pid = syscall(syscall_t::WAIT, target_pid, _IMM(&status), nil);
	if (pid > 0 && infop) {
		infop->si_pid = pid;
		infop->si_status = status;
		infop->si_code = WEXITED;
		return 0;
	}
	return (pid == 0 && (options & WNOHANG)) ? 0 : -1;
}

int execve(const char* path, char* const argv[], char* const envp[]) {
	return int(ProcessIdResult(syscall(syscall_t::EXET, _IMM(path), _IMM(argv), _IMM(envp))));
}

extern "C" char** environ;
int execv(const char* path, char* const argv[]) {
	return execve(path, argv, environ);
}

namespace {
	struct PosixSpawnAction {
		TaskmanExecAction request;
		char* path;
	};

	constexpr short PosixSpawnFlags = POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP |
		POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK |
		POSIX_SPAWN_SETSCHEDPARAM | POSIX_SPAWN_SETSCHEDULER;

	int AppendSpawnAction(posix_spawn_file_actions_t* actions,
		const TaskmanExecAction& request, const char* path = nullptr) {
		if (!actions || (actions->__count && !actions->__actions)) return EINVAL;
		if (actions->__count == actions->__capacity) {
			if (actions->__capacity > (~size_t(0) / 2) / sizeof(PosixSpawnAction)) return EAGAIN;
			const size_t capacity = actions->__capacity ? actions->__capacity * 2 : 4;
			PosixSpawnAction* next = (PosixSpawnAction*)malloc(capacity * sizeof(PosixSpawnAction));
			if (!next) return ENOMEM;
			auto* old = (PosixSpawnAction*)actions->__actions;
			for (size_t i = 0; i < actions->__count; i++) next[i] = old[i];
			free(old);
			actions->__actions = next;
			actions->__capacity = capacity;
		}
		char* path_copy = nullptr;
		if (path) {
			const stduint length = StrLength(path);
			if (length == ~stduint(0)) return ENAMETOOLONG;
			path_copy = (char*)malloc(length + 1);
			if (!path_copy) return ENOMEM;
			MemCopyN(path_copy, path, length + 1);
		}
		auto* entries = (PosixSpawnAction*)actions->__actions;
		entries[actions->__count].request = request;
		entries[actions->__count].path = path_copy;
		entries[actions->__count].request.path = path_copy;
		actions->__count++;
		return 0;
	}

	int SpawnWithActions(pid_t* pid, const char* path,
		const posix_spawn_file_actions_t* file_actions,
		const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
		if (!pid || !path || !argv) return EINVAL;
		if (file_actions && file_actions->__count && !file_actions->__actions) return EINVAL;
		if (attrp && attrp->__flags) return ENOTSUP;

		const size_t action_count = file_actions ? file_actions->__count : 0;
		TaskmanExecAction* requests = nullptr;
		if (action_count) {
			if (action_count > ~size_t(0) / sizeof(*requests)) return EAGAIN;
			requests = (TaskmanExecAction*)malloc(action_count * sizeof(*requests));
			if (!requests) return ENOMEM;
			auto* entries = (PosixSpawnAction*)file_actions->__actions;
			for (size_t i = 0; i < action_count; i++) requests[i] = entries[i].request;
		}

		TaskmanSpawnRequest request = { envp, requests, action_count };
		const stduint tagged_request = _IMM(&request) | syscall_exec_spawn_request_tag;
		const stdsint result = stdsint(syscall(syscall_t::EXEC,
			_IMM(path), _IMM(argv), tagged_request));
		free(requests);
		if (result < 0) return int(-result);
		if (!result) return EIO;
		*pid = pid_t(result);
		return 0;
	}
}

extern "C" int posix_spawn_file_actions_init(posix_spawn_file_actions_t* actions) {
	if (!actions) return EINVAL;
	actions->__actions = nullptr;
	actions->__count = 0;
	actions->__capacity = 0;
	return 0;
}

extern "C" int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* actions) {
	if (!actions) return EINVAL;
	auto* entries = (PosixSpawnAction*)actions->__actions;
	for (size_t i = 0; i < actions->__count; i++) free(entries[i].path);
	free(entries);
	actions->__actions = nullptr;
	actions->__count = 0;
	actions->__capacity = 0;
	return 0;
}

extern "C" int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* actions,
	int fd, const char* path, int oflag, mode_t mode) {
	if (!actions || fd < 0 || !path) return EINVAL;
	TaskmanExecAction request{};
	request.type = TaskmanExecActionType::Open;
	request.fd = fd;
	request.flags = stduint(oflag);
	request.mode = stduint(mode);
	return AppendSpawnAction(actions, request, path);
}

extern "C" int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* actions, int fd) {
	if (!actions || fd < 0) return EINVAL;
	TaskmanExecAction request{};
	request.type = TaskmanExecActionType::Close;
	request.fd = fd;
	return AppendSpawnAction(actions, request);
}

extern "C" int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* actions,
	int fd, int newfd) {
	if (!actions || fd < 0 || newfd < 0) return EINVAL;
	TaskmanExecAction request{};
	request.type = TaskmanExecActionType::Dup2;
	request.fd = fd;
	request.newfd = newfd;
	return AppendSpawnAction(actions, request);
}

extern "C" int posix_spawnattr_init(posix_spawnattr_t* attr) {
	if (!attr) return EINVAL;
	*attr = {};
	return 0;
}

extern "C" int posix_spawnattr_destroy(posix_spawnattr_t* attr) {
	if (!attr) return EINVAL;
	*attr = {};
	return 0;
}

extern "C" int posix_spawnattr_getflags(const posix_spawnattr_t* attr, short* flags) {
	if (!attr || !flags) return EINVAL;
	*flags = attr->__flags;
	return 0;
}

extern "C" int posix_spawnattr_setflags(posix_spawnattr_t* attr, short flags) {
	if (!attr || (flags & ~PosixSpawnFlags)) return EINVAL;
	attr->__flags = flags;
	return 0;
}

extern "C" int posix_spawnattr_getpgroup(const posix_spawnattr_t* attr, pid_t* pgroup) {
	if (!attr || !pgroup) return EINVAL;
	*pgroup = attr->__pgroup;
	return 0;
}

extern "C" int posix_spawnattr_setpgroup(posix_spawnattr_t* attr, pid_t pgroup) {
	if (!attr || pgroup < 0) return EINVAL;
	attr->__pgroup = pgroup;
	return 0;
}

extern "C" int posix_spawnattr_getsigdefault(const posix_spawnattr_t* attr,
	sigset_t* sigdefault) {
	if (!attr || !sigdefault) return EINVAL;
	*sigdefault = attr->__sigdefault;
	return 0;
}

extern "C" int posix_spawnattr_setsigdefault(posix_spawnattr_t* attr,
	const sigset_t* sigdefault) {
	if (!attr || !sigdefault) return EINVAL;
	attr->__sigdefault = *sigdefault;
	return 0;
}

extern "C" int posix_spawnattr_getsigmask(const posix_spawnattr_t* attr,
	sigset_t* sigmask) {
	if (!attr || !sigmask) return EINVAL;
	*sigmask = attr->__sigmask;
	return 0;
}

extern "C" int posix_spawnattr_setsigmask(posix_spawnattr_t* attr,
	const sigset_t* sigmask) {
	if (!attr || !sigmask) return EINVAL;
	attr->__sigmask = *sigmask;
	return 0;
}

extern "C" int posix_spawnattr_getschedpolicy(const posix_spawnattr_t* attr,
	int* schedpolicy) {
	if (!attr || !schedpolicy) return EINVAL;
	*schedpolicy = attr->__schedpolicy;
	return 0;
}

extern "C" int posix_spawnattr_setschedpolicy(posix_spawnattr_t* attr, int schedpolicy) {
	if (!attr) return EINVAL;
	attr->__schedpolicy = schedpolicy;
	return 0;
}

extern "C" int posix_spawnattr_getschedparam(const posix_spawnattr_t* attr,
	struct sched_param* schedparam) {
	if (!attr || !schedparam) return EINVAL;
	MemCopyN(schedparam, &attr->__schedpriority, sizeof(attr->__schedpriority));
	return 0;
}

extern "C" int posix_spawnattr_setschedparam(posix_spawnattr_t* attr,
	const struct sched_param* schedparam) {
	if (!attr || !schedparam) return EINVAL;
	MemCopyN(&attr->__schedpriority, schedparam, sizeof(attr->__schedpriority));
	return 0;
}

extern "C" int posix_spawn(pid_t* pid, const char* path,
	const posix_spawn_file_actions_t* file_actions,
	const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
	return SpawnWithActions(pid, path, file_actions, attrp, argv, envp);
}

extern "C" int posix_spawnp(pid_t* pid, const char* file,
	const posix_spawn_file_actions_t* file_actions,
	const posix_spawnattr_t* attrp, char* const argv[], char* const envp[]) {
	if (!file || !*file) return ENOENT;
	for (const char* p = file; *p; p++) if (*p == '/')
		return SpawnWithActions(pid, file, file_actions, attrp, argv, envp);

	extern char* getenv(const char* name);
	const char* path = getenv("PATH");
	if (!path) path = "/bin:/usr/bin:/md0";
	const stduint file_length = StrLength(file);
	const stduint path_length = StrLength(path);
	if (path_length > ~stduint(0) - file_length - 2) return ENAMETOOLONG;
	char* candidate = (char*)malloc(path_length + file_length + 2);
	if (!candidate) return ENOMEM;
	int denied = 0;
	const char* component = path;
	for (;;) {
		const char* end = component;
		while (*end && *end != ':') end++;
		const stduint directory_length = stduint(end - component);
		stduint position = 0;
		if (directory_length) {
			MemCopyN(candidate, component, directory_length);
			position = directory_length;
			if (candidate[position - 1] != '/') candidate[position++] = '/';
		}
		MemCopyN(candidate + position, file, file_length + 1);
		const int status = SpawnWithActions(pid, candidate, file_actions, attrp, argv, envp);
		if (!status) {
			free(candidate);
			return 0;
		}
		if (status == EACCES) denied = status;
		else if (status != ENOENT && status != ENOTDIR) {
			free(candidate);
			return status;
		}
		if (!*end) break;
		component = end + 1;
	}
	free(candidate);
	return denied ? denied : ENOENT;
}

