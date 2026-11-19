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
	const uint32 shared_members[] = { 1000 };
	const GroupRecord groups[] = {
		{ 0, "root", nullptr, 0 },
		{ 1000, "user", nullptr, 0 },
		{ 1001, "shared", shared_members, numsof(shared_members) },
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

// Binary search reads an immutable sorted set without taking the process identity lock.
bool SupplementaryGroups::Contains(uint32 gid) const {
	stduint first = 0, last = Count();
	while (first < last) {
		const stduint middle = first + (last - first) / 2;
		if (At(middle) < gid) first = middle + 1;
		else last = middle;
	}
	return first < Count() && At(first) == gid;
}

// Normalize in place with an iterative heap sort so large dynamic sets need neither recursion nor scratch storage.
void Userman::NormalizeGroups(SupplementaryGroups& groups) {
	auto& ids = groups.ids;
	auto sift = [&](stduint root, stduint count) {
		while (root < count / 2) {
			stduint child = root * 2 + 1;
			if (child + 1 < count && ids[child] < ids[child + 1]) child++;
			if (ids[root] >= ids[child]) break;
			ids.Exchange(root, child);
			root = child;
		}
	};
	for (stduint i = ids.Count() / 2; i; i--) sift(i - 1, ids.Count());
	for (stduint end = ids.Count(); end > 1; end--) {
		ids.Exchange(0, end - 1);
		sift(0, end - 1);
	}
	stduint kept = 0;
	for (stduint i = 0; i < groups.ids.Count(); i++) {
		const uint32 gid = groups.ids[i];
		if (!kept || groups.ids[kept - 1] != gid) groups.ids[kept++] = gid;
	}
	groups.ids.Remove(kept, groups.ids.Count() - kept);
}

bool Userman::HasGroup(const ProcessCredentials& credentials, uint32 gid) {
	return credentials.gid == gid || (credentials.groups && credentials.groups->Contains(gid));
}

int Userman::SetGroups(ProcessBlock& process, const stdsint* input, stduint count) {
	if (count > 0x7FFFFFFFu || (count && !input)) return -EINVAL;
	uni::SharedPtr<SupplementaryGroups> builder;
	if (count) {
		builder.reset(new (std::nothrow_t{}) SupplementaryGroups());
		if (!builder) return -ENOMEM;
		for (stduint i = 0; i < count; i++) {
			if (input[i] < 0 || uint64(input[i]) > 0x7FFFFFFFu) return -EINVAL;
			if (!builder->ids.Append(uint32(input[i]))) return -ENOMEM;
		}
		NormalizeGroups(*builder);
	}
	uni::SharedPtr<const SupplementaryGroups> next(static_cast<uni::SharedPtr<SupplementaryGroups>&&>(builder));
	uni::SharedPtr<const SupplementaryGroups> previous;
	{
		MutexLocal guard(&process.mutex_credentials);
		if (process.credentials.uid != 0) return -EPERM;
		previous.swap(process.credentials.groups);
		process.credentials.groups = next;
	}
	// The last old reference and its potentially large vector are released outside the identity lock.
	return 0;
}

int Userman::AccountGroups(const char* name, uint32 base, uni::SharedPtr<const SupplementaryGroups>& result) {
	const auto* user = FindUser(name);
	if (!user) return -ENOENT;
	if (base > 0x7FFFFFFFu) return -EINVAL;
	uni::SharedPtr<SupplementaryGroups> builder(new (std::nothrow_t{}) SupplementaryGroups());
	if (!builder || !builder->ids.Append(base)) return -ENOMEM;
	for (const auto& group : groups) {
		for (stduint i = 0; i < group.member_count; i++) {
			if (group.members[i] != user->uid) continue;
			if (!builder->ids.Append(group.gid)) return -ENOMEM;
			break;
		}
	}
	if (builder->Count() > 0x7FFFFFFFu) return -EOVERFLOW;
	NormalizeGroups(*builder);
	result = static_cast<uni::SharedPtr<SupplementaryGroups>&&>(builder);
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

// Root selects an account directly; authenticate other callers before atomically replacing IDs and groups.
int Userman::Login(ProcessBlock& process, const char* name, const char* password) {
	const auto* user = FindUser(name);
	if (!user) return -EACCES;
	ProcessCredentials next;
	next.uid = user->uid;
	next.gid = user->gid;
	const int status = AccountGroups(name, user->gid, next.groups);
	if (status) return status;
	uni::SharedPtr<const SupplementaryGroups> previous;
	{
		MutexLocal guard(&process.mutex_credentials);
		if (process.credentials.uid != 0 && (!password || StrCompare(password, "p"))) return -EACCES;
		previous.swap(process.credentials.groups);
		process.credentials = next;
	}
	return 0;
}
