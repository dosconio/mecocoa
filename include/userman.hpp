#ifndef USERMAN_HPP_
#define USERMAN_HPP_

#include <c/stdinc.h>

// Numeric credentials are independent of account names, storage, and authentication.
struct ProcessCredentials {
	uint32 uid = 0;
	uint32 gid = 0;
};

struct ProcessCreationContext {
	ProcessCredentials credentials;
	uint32 mask = 0022;
};

class ProcessBlock;

struct UserRecord {
	uint32 uid;
	uint32 gid;
	const char* name;
	const char* home;
	const char* shell;
};

struct GroupRecord {
	uint32 gid;
	const char* name;
};

class Userman {
public:
	// Built-in records are immutable and their strings remain valid for the kernel lifetime.
	static const UserRecord* FindUser(uint32 uid);
	static const UserRecord* FindUser(const char* name);
	static const GroupRecord* FindGroup(uint32 gid);
	static const GroupRecord* FindGroup(const char* name);
	// Return one coherent value and release the identity lock before entering another subsystem.
	static ProcessCredentials GetCredentials(ProcessBlock& process);
	// Snapshot identity and creation policy under one lock; umask only removes rwx bits on creation.
	static ProcessCreationContext GetCreationContext(ProcessBlock& process);
	static uint32 ExchangeCreationMask(ProcessBlock& process, uint32 mask);
	static void InitializeCreationMask(ProcessBlock& process, uint32 mask);
	// The destination must be unpublished; initialization never acquires a second process lock.
	static void InitializeCredentials(ProcessBlock& process, const ProcessCredentials& credentials);
	static void InheritCredentials(ProcessBlock& process, ProcessBlock& parent);
	// Root may change numeric IDs; an unprivileged process may only retain its current ID.
	static int SetUserID(ProcessBlock& process, uint32 uid);
	static int SetGroupID(ProcessBlock& process, uint32 gid);
	static bool IsRoot(ProcessBlock& process);
	static bool MayControl(ProcessBlock& caller, ProcessBlock& target);
	// Authenticate and replace both IDs together; the prototype password is deliberately fixed to p.
	static int Login(ProcessBlock& process, const char* name, const char* password);
};

#endif
