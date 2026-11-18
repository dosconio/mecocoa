// ASCII C++ TAB4 CRLF
// ModuTitle: Process identities and built-in user accounts
#include "../include/mecocoa.hpp"
#include <c/ustring.h>
#include <c/ISO_IEC_STD/errno.h>

//{} a b c  grp{ab ac bc}

namespace {
	// Account storage is intentionally static until persistent account management is introduced.
	const UserRecord users[] = {
		{ 0, 0, "root", "/", "/md0/cot" },
		{ 1000, 1000, "user", "/home", "/md0/cot" },
	};
	const GroupRecord groups[] = {
		{ 0, "root" },
		{ 1000, "user" },
	};
}

const UserRecord* Userman::FindUser(uint32 uid) {
	for (const auto& user : users) if (user.uid == uid) return &user;
	return nullptr;
}

const UserRecord* Userman::FindUser(const char* name) {
	if (name) for (const auto& user : users) if (!StrCompare(user.name, name)) return &user;
	return nullptr;
}

const GroupRecord* Userman::FindGroup(uint32 gid) {
	for (const auto& group : groups) if (group.gid == gid) return &group;
	return nullptr;
}

const GroupRecord* Userman::FindGroup(const char* name) {
	if (name) for (const auto& group : groups) if (!StrCompare(group.name, name)) return &group;
	return nullptr;
}

ProcessCredentials Userman::GetCredentials(ProcessBlock& process) {
	MutexLocal guard(&process.mutex_credentials);
	return process.credentials;
}

void Userman::InitializeCredentials(ProcessBlock& process, const ProcessCredentials& credentials) {
	// Only loaders and fork may initialize an unpublished process without taking its identity lock.
	process.credentials = credentials;
}

ProcessCreationContext Userman::GetCreationContext(ProcessBlock& process) {
	MutexLocal guard(&process.mutex_credentials);
	return { process.credentials, process.creation_mask };
}

uint32 Userman::ExchangeCreationMask(ProcessBlock& process, uint32 mask) {
	MutexLocal guard(&process.mutex_credentials);
	const uint32 previous = process.creation_mask;
	process.creation_mask = mask & 0777;
	return previous;
}

void Userman::InitializeCreationMask(ProcessBlock& process, uint32 mask) {
	// Like credential initialization, this is restricted to unpublished destination processes.
	process.creation_mask = mask & 0777;
}

void Userman::InheritCredentials(ProcessBlock& process, ProcessBlock& parent) {
	const auto context = GetCreationContext(parent);
	InitializeCredentials(process, context.credentials);
	InitializeCreationMask(process, context.mask);
}

int Userman::SetUserID(ProcessBlock& process, uint32 uid) {
	if (uid > 0x7FFFFFFFu) return -EINVAL;
	MutexLocal guard(&process.mutex_credentials);
	auto* credentials = &process.credentials;
	if (credentials->uid != 0 && credentials->uid != uid) return -EPERM;
	credentials->uid = uid;
	return 0;
}

// Group changes are authorized by UID, including when an unprivileged process still has GID zero.
int Userman::SetGroupID(ProcessBlock& process, uint32 gid) {
	if (gid > 0x7FFFFFFFu) return -EINVAL;
	MutexLocal guard(&process.mutex_credentials);
	auto* credentials = &process.credentials;
	if (credentials->uid != 0 && credentials->gid != gid) return -EPERM;
	credentials->gid = gid;
	return 0;
}

bool Userman::IsRoot(ProcessBlock& process) {
	return GetCredentials(process).uid == 0;
}

bool Userman::MayControl(ProcessBlock& caller, ProcessBlock& target) {
	const auto source = GetCredentials(caller);
	const auto destination = GetCredentials(target);
	return source.uid == 0 || source.uid == destination.uid;
}

// Root selects an account directly; other callers must authenticate before either ID changes.
int Userman::Login(ProcessBlock& process, const char* name, const char* password) {
	const auto* user = FindUser(name);
	if (!user) return -EACCES;
	MutexLocal guard(&process.mutex_credentials);
	auto* credentials = &process.credentials;
	if (credentials->uid != 0 && (!password || StrCompare(password, "p"))) return -EACCES;
	credentials->gid = user->gid;
	credentials->uid = user->uid;
	return 0;
}
