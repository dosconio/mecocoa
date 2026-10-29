#include <stdio.h>
#include <c/format/ELF.h>

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define printf(...)

namespace {
	constexpr unsigned int LibraryPageSize = 0x1000;

	enum class LibraryPageKind : unsigned char {
		Hole,
		File,
		Private,
		Zero,
	};

	struct LibraryPagePlan {
		LibraryPageKind kind = LibraryPageKind::Hole;
		unsigned int flags = 0;
		unsigned int file_offset = 0;
	};

	struct LibraryLoadPlan {
		unsigned int first_page = 0;
		unsigned int end_page = 0;
		unsigned int alignment = LibraryPageSize;
		LibraryPagePlan* pages = nullptr;
		~LibraryLoadPlan() { delete[] pages; }
	};

	struct LibraryMappingOwner {
		void* address = nullptr;
		unsigned int length = 0;
		~LibraryMappingOwner() {
			if (address && munmap(address, length))
				plogerro("[LD-MCCA] failed to release mapping at %08x",
					(unsigned int)address);
		}
		// The process VMA owns a successful image after this call.
		void Release() { address = nullptr; length = 0; }
	};

	bool ReadLibraryBytes(int fd, unsigned int offset, void* target,
		unsigned int length, unsigned int file_size) {
		if (offset > file_size || length > file_size - offset ||
			offset > 0x7FFFFFFF ||
			lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset) return false;
		unsigned int done = 0;
		while (done < length) {
			const stdsint count = read(fd, (char*)target + done, length - done);
			if (count <= 0) return false;
			done += (unsigned int)count;
		}
		return true;
	}

	bool LibraryFileOffset(const ELF_PHT_t* headers, unsigned int count,
		unsigned int address, unsigned int length, unsigned int& offset) {
		for (unsigned int i = 0; i < count; i++) {
			const auto& ph = headers[i];
			if (ph.p_type != PT_LOAD || address < ph.p_vaddr ||
				address - ph.p_vaddr > ph.p_filesz ||
				length > ph.p_filesz - (address - ph.p_vaddr)) continue;
			offset = ph.p_offset + address - ph.p_vaddr;
			return true;
		}
		return false;
	}

	bool PlanLibraryRelocations(int fd, unsigned int file_size,
		const ELF_PHT_t* headers, unsigned int count, LibraryLoadPlan& plan) {
		unsigned int rel = 0, rel_size = 0, jump_rel = 0, jump_size = 0;
		unsigned int rel_entry = sizeof(Elf32_Rel), plt_type = DT_REL;
		bool found_dynamic = false;
		for (unsigned int i = 0; i < count; i++) {
			const auto& ph = headers[i];
			if (ph.p_type != PT_DYNAMIC) continue;
			if (found_dynamic || ph.p_filesz > file_size ||
				ph.p_offset > file_size - ph.p_filesz ||
				ph.p_filesz % sizeof(Elf32_Dyn)) return false;
			unsigned int mapped_offset = 0;
			if (!LibraryFileOffset(headers, count, ph.p_vaddr,
				ph.p_filesz, mapped_offset) || mapped_offset != ph.p_offset)
				return false;
			found_dynamic = true;
			bool terminated = false;
			for (unsigned int j = 0; j < ph.p_filesz / sizeof(Elf32_Dyn); j++) {
				Elf32_Dyn entry = {};
				if (!ReadLibraryBytes(fd, ph.p_offset + j * sizeof(entry),
					&entry, sizeof(entry), file_size)) return false;
				if (entry.d_tag == DT_NULL) { terminated = true; break; }
				switch (entry.d_tag) {
				case DT_REL: rel = entry.d_un.d_ptr; break;
				case DT_RELSZ: rel_size = entry.d_un.d_val; break;
				case DT_RELENT: rel_entry = entry.d_un.d_val; break;
				case DT_JMPREL: jump_rel = entry.d_un.d_ptr; break;
				case DT_PLTRELSZ: jump_size = entry.d_un.d_val; break;
				case DT_PLTREL: plt_type = entry.d_un.d_val; break;
				case DT_RELA:
				case DT_RELASZ:
				case DT_TEXTREL: return false;
				default: break;
				}
			}
			if (!terminated) return false;
		}
		if (!found_dynamic || rel_entry != sizeof(Elf32_Rel) ||
			plt_type != DT_REL) return false;
		for (unsigned int table = 0; table < 2; table++) {
			const unsigned int address = table ? jump_rel : rel;
			const unsigned int size = table ? jump_size : rel_size;
			if (!size) continue;
			if (!address || size % sizeof(Elf32_Rel)) return false;
			unsigned int offset = 0;
			if (!LibraryFileOffset(headers, count, address, size, offset))
				return false;
			const unsigned int count_rel = size / sizeof(Elf32_Rel);
			Elf32_Rel* items = new Elf32_Rel[count_rel];
			if (!items) return false;
			if (!ReadLibraryBytes(fd, offset, items, size, file_size)) {
				delete[] items;
				return false;
			}
			bool valid = true;
			for (unsigned int j = 0; j < count_rel; j++) {
				const auto& item = items[j];
				const unsigned int type = ELF32_R_TYPE(item.r_info);
				if (type != R_386_RELATIVE && type != R_386_32 &&
					type != R_386_GLOB_DAT && type != R_386_JUMP_SLOT) {
					valid = false;
					break;
				}
				bool writable = false;
				for (unsigned int k = 0; k < count; k++) {
					const auto& ph = headers[k];
					if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_W) ||
						item.r_offset < ph.p_vaddr ||
						item.r_offset - ph.p_vaddr > ph.p_memsz ||
						sizeof(unsigned int) > ph.p_memsz -
							(item.r_offset - ph.p_vaddr)) continue;
					writable = true;
					break;
				}
				if (!writable) {
					valid = false;
					break;
				}
			}
			delete[] items;
			if (!valid) return false;
		}
		return true;
	}

	bool PlanLibraryLoads(int fd, unsigned int file_size,
		const ELF_Header_t& header, const ELF_PHT_t* headers,
		LibraryLoadPlan& plan) {
		if (header.MagicNumber[0] != 0x7F ||
			header.MagicNumber[1] != 'E' || header.MagicNumber[2] != 'L' ||
			header.MagicNumber[3] != 'F' || header.Bitmode != ELF32 ||
			header.Endianness != 1 || header.e_type != ET_DYN ||
			header.e_machine != EM_386 || header.e_ehsize != sizeof(header) ||
			!header.e_phnum || header.e_phentsize != sizeof(ELF_PHT_t) ||
			header.e_phoff > file_size ||
			header.e_phnum > (file_size - header.e_phoff) / sizeof(ELF_PHT_t))
			return false;
		unsigned int first = ~0u, last = 0;
		for (unsigned int i = 0; i < header.e_phnum; i++) {
			const auto& ph = headers[i];
			if (ph.p_type != PT_LOAD) continue;
			if (ph.p_filesz > ph.p_memsz || ph.p_offset > file_size ||
				ph.p_filesz > file_size - ph.p_offset ||
				((ph.p_offset ^ ph.p_vaddr) & (LibraryPageSize - 1)) ||
				(ph.p_align > 1 && ((ph.p_align & (ph.p_align - 1)) ||
					((ph.p_vaddr - ph.p_offset) & (ph.p_align - 1)))) ||
				ph.p_vaddr > ~0u - (LibraryPageSize - 1) ||
				ph.p_memsz > ~0u - (LibraryPageSize - 1) - ph.p_vaddr)
				return false;
			if (!ph.p_memsz) continue;
			for (unsigned int j = 0; j < i; j++) {
				const auto& other = headers[j];
				if (other.p_type != PT_LOAD || !other.p_memsz) continue;
				if (ph.p_vaddr < other.p_vaddr + other.p_memsz &&
					other.p_vaddr < ph.p_vaddr + ph.p_memsz) return false;
			}
			const unsigned int begin = ph.p_vaddr & ~(LibraryPageSize - 1);
			const unsigned int end = (ph.p_vaddr + ph.p_memsz +
				LibraryPageSize - 1) & ~(LibraryPageSize - 1);
			if (begin < first) first = begin;
			if (end > last) last = end;
			if (ph.p_align > plan.alignment) plan.alignment = ph.p_align;
		}
		if (first == ~0u || last <= first ||
			(last - first) / LibraryPageSize > ~0u / sizeof(LibraryPagePlan))
			return false;
		plan.first_page = first;
		plan.end_page = last;
		const unsigned int page_count = (last - first) / LibraryPageSize;
		plan.pages = new LibraryPagePlan[page_count];
		if (!plan.pages) return false;
		for (unsigned int page = first; page < last; page += LibraryPageSize) {
			auto& entry = plan.pages[(page - first) / LibraryPageSize];
			unsigned int covering = 0, full_file = ~0u;
			bool has_file = false;
			for (unsigned int i = 0; i < header.e_phnum; i++) {
				const auto& ph = headers[i];
				if (ph.p_type != PT_LOAD || !ph.p_memsz ||
					page >= ph.p_vaddr + ph.p_memsz ||
					ph.p_vaddr >= page + LibraryPageSize) continue;
				covering++;
				entry.flags |= ph.p_flags;
				if (page < ph.p_vaddr + ph.p_filesz &&
					ph.p_vaddr < page + LibraryPageSize) has_file = true;
				if (page >= ph.p_vaddr && ph.p_filesz >= LibraryPageSize &&
					page - ph.p_vaddr <= ph.p_filesz - LibraryPageSize &&
					!((ph.p_offset + page - ph.p_vaddr) &
						(LibraryPageSize - 1))) full_file = i;
			}
			if (!covering) continue;
			if (covering == 1 && full_file != ~0u) {
				const auto& ph = headers[full_file];
				entry.kind = LibraryPageKind::File;
				entry.file_offset = ph.p_offset + page - ph.p_vaddr;
			}
			else if (has_file) {
				entry.kind = LibraryPageKind::Private;
			}
			else {
				entry.kind = LibraryPageKind::Zero;
			}
		}
		return PlanLibraryRelocations(fd, file_size, headers,
			header.e_phnum, plan);
	}

	int LibraryPageProtection(unsigned int flags) {
		// Mecocoa requires PROT_READ for every present user mapping.
		int protection = PROT_READ;
		if (flags & PF_W) protection |= PROT_WRITE;
		if (flags & PF_X) protection |= PROT_EXEC;
		return protection;
	}

	bool MapLibraryPages(int fd, unsigned int file_size,
		const ELF_PHT_t* headers, unsigned int header_count,
		const LibraryLoadPlan& plan, char*& image_base,
		LibraryMappingOwner& owner) {
		const unsigned int image_size = plan.end_page - plan.first_page;
		if (!image_size || image_size > ~0u - plan.alignment) return false;
		const unsigned int reserve_size = image_size + plan.alignment -
			LibraryPageSize;
		void* reservation = mmap(nullptr, reserve_size, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (reservation == MAP_FAILED) return false;
		const unsigned int raw = (unsigned int)reservation;
		if (reserve_size > ~0u - raw) {
			munmap(reservation, reserve_size);
			return false;
		}
		// The load bias must satisfy every PT_LOAD p_align congruence.
		const unsigned int image_start = raw +
			((plan.first_page - raw) & (plan.alignment - 1));
		if (image_start < raw || image_size > ~0u - image_start ||
			image_start + image_size > raw + reserve_size) {
			munmap(reservation, reserve_size);
			return false;
		}
		const unsigned int bias = image_start - plan.first_page;
		const unsigned int page_count = image_size / LibraryPageSize;
		for (unsigned int index = 0; index < page_count;) {
			const LibraryPagePlan& entry = plan.pages[index];
			if (entry.kind == LibraryPageKind::Hole) {
				index++;
				continue;
			}
			unsigned int end = index + 1;
			while (end < page_count &&
				plan.pages[end].kind == entry.kind &&
				plan.pages[end].flags == entry.flags &&
				(entry.kind != LibraryPageKind::File ||
					(entry.file_offset <= ~0u -
						(end - index) * LibraryPageSize &&
						plan.pages[end].file_offset == entry.file_offset +
							(end - index) * LibraryPageSize))) end++;
			const unsigned int address = image_start + index * LibraryPageSize;
			const unsigned int length = (end - index) * LibraryPageSize;
			const bool file_page = entry.kind == LibraryPageKind::File;
			const int protection = file_page ?
				LibraryPageProtection(entry.flags) : PROT_READ | PROT_WRITE;
			// File VMAs start on cached pages; writable MAP_PRIVATE mappings separate on first write.
			void* mapped = mmap((void*)address, length, protection,
				MAP_PRIVATE | MAP_FIXED | (file_page ? 0 : MAP_ANONYMOUS),
				file_page ? fd : -1, file_page ? entry.file_offset : 0);
			if (mapped != (void*)address) {
				plogerro("[LD-MCCA] mmap failed at %08x length=%u kind=%u",
					address, length, (unsigned int)entry.kind);
				munmap(reservation, reserve_size);
				return false;
			}
			if (!file_page) {
				for (unsigned int page = index; page < end; page++) {
					if (plan.pages[page].kind != LibraryPageKind::Private)
						continue;
					// ELF gABI PT_LOAD: bytes beyond p_filesz remain zero-filled.
					const unsigned int page_address = plan.first_page +
						page * LibraryPageSize;
					for (unsigned int i = 0; i < header_count; i++) {
						const ELF_PHT_t& ph = headers[i];
						if (ph.p_type != PT_LOAD || !ph.p_filesz ||
							page_address >= ph.p_vaddr + ph.p_filesz ||
							ph.p_vaddr >= page_address + LibraryPageSize)
							continue;
						const unsigned int begin = page_address > ph.p_vaddr ?
							page_address : ph.p_vaddr;
						const unsigned int file_end = ph.p_vaddr + ph.p_filesz;
						const unsigned int finish = page_address + LibraryPageSize <
							file_end ? page_address + LibraryPageSize : file_end;
						if (!ReadLibraryBytes(fd, ph.p_offset + begin - ph.p_vaddr,
							(void*)(bias + begin), finish - begin, file_size)) {
							plogerro("[LD-MCCA] boundary page read failed at %08x",
								bias + begin);
							munmap(reservation, reserve_size);
							return false;
						}
					}
				}
				if (protection != LibraryPageProtection(entry.flags) &&
					mprotect((void*)address, length,
						LibraryPageProtection(entry.flags))) {
					plogerro("[LD-MCCA] mprotect failed at %08x length=%u",
						address, length);
					munmap(reservation, reserve_size);
					return false;
				}
			}
			index = end;
		}
		image_base = (char*)bias;
		owner.address = reservation;
		owner.length = reserve_size;
		return true;
	}

	bool ProtectLibraryRELRO(const ELF_PHT_t* headers,
		unsigned int header_count, const LibraryLoadPlan& plan,
		char* image_base) {
		for (unsigned int i = 0; i < header_count; i++) {
			const auto& ph = headers[i];
			if (ph.p_type != PT_GNU_RELRO || !ph.p_memsz) continue;
			if (ph.p_vaddr > ~0u - ph.p_memsz) return false;
			// PT_GNU_RELRO is protected at page granularity after relocation.
			const unsigned int begin = ph.p_vaddr & ~(LibraryPageSize - 1);
			const unsigned int end = (ph.p_vaddr + ph.p_memsz) &
				~(LibraryPageSize - 1);
			if (begin >= end) continue;
			if (begin < plan.first_page || end > plan.end_page) return false;
			for (unsigned int page = begin; page < end; page += LibraryPageSize) {
				const auto& entry = plan.pages[(page - plan.first_page) /
					LibraryPageSize];
				if (entry.kind == LibraryPageKind::Hole || !(entry.flags & PF_W))
					return false;
				const int protection = PROT_READ |
					((entry.flags & PF_X) ? PROT_EXEC : 0);
				if (mprotect(image_base + page, LibraryPageSize, protection))
					return false;
			}
		}
		return true;
	}
}

int main(int argc, char* argv[], char* envp[]) {
	#if _ACCM == 0x8632
	constexpr const char* runtime_arch_dir = "x86";
	#elif _ACCM == 0x8664
	constexpr const char* runtime_arch_dir = "x64";
	#endif
	printf("\n========================================\n");
	printf("[LD-MCCA] Hello from the Dummy Interpreter!\n");
	printf("[LD-MCCA] Argc: %d\n", argc);
	
	// Find the end of envp to locate auxv
	char** auxv_ptr = envp;
	while (*auxv_ptr != nullptr) {
		auxv_ptr++;
	}
	auxv_ptr++; // Skip the NULL terminator of envp
	
	unsigned int* auxv = (unsigned int*)auxv_ptr;
	
	unsigned int at_phdr = 0;
	unsigned int at_phent = 0;
	unsigned int at_phnum = 0;
	unsigned int at_entry = 0;
	
	while (*auxv != AT_NULL) {
		unsigned int type = *auxv++;
		unsigned int val = *auxv++;
		if (type == AT_PHDR) at_phdr = val;
		if (type == AT_PHENT) at_phent = val;
		if (type == AT_PHNUM) at_phnum = val;
		if (type == AT_ENTRY) at_entry = val;
	}
	
	printf("[LD-MCCA] Extracted AT_PHDR: 0x%08x (ENT:%d, NUM:%d)\n", at_phdr, at_phent, at_phnum);
	printf("[LD-MCCA] Extracted AT_ENTRY: 0x%08x\n", at_entry);
	
	// Parse Program Headers to find PT_DYNAMIC
	unsigned int dynamic_addr = 0;
	for (unsigned int i = 0; i < at_phnum; i++) {
		ELF_PHT_t* ph = (ELF_PHT_t*)(at_phdr + i * at_phent);
		
		if (ph->p_type == PT_DYNAMIC) {
			dynamic_addr = ph->p_vaddr;
			printf("[LD-MCCA] Found PT_DYNAMIC at 0x%08x\n", dynamic_addr);
			break;
		}
	}
	
	// Parse the .dynamic section
	unsigned int dt_strtab = 0;
	unsigned int dt_symtab = 0;
	unsigned int dt_jmprel = 0;
	unsigned int dt_pltrelsz = 0;
	unsigned int dt_rel = 0;
	unsigned int dt_relsz = 0;
	
	if (dynamic_addr) {
		Elf32_Dyn* dyn = (Elf32_Dyn*)dynamic_addr;
		while (dyn->d_tag != DT_NULL) {
			if (dyn->d_tag == DT_STRTAB) dt_strtab = dyn->d_un.d_ptr;
			else if (dyn->d_tag == DT_SYMTAB) dt_symtab = dyn->d_un.d_ptr;
			else if (dyn->d_tag == DT_JMPREL) dt_jmprel = dyn->d_un.d_ptr;
			else if (dyn->d_tag == DT_PLTRELSZ) dt_pltrelsz = dyn->d_un.d_val;
			else if (dyn->d_tag == DT_REL) dt_rel = dyn->d_un.d_ptr;
			else if (dyn->d_tag == DT_RELSZ) dt_relsz = dyn->d_un.d_val;
			dyn++;
		}
		
		printf("[LD-MCCA] DT_STRTAB: 0x%08x\n", dt_strtab);
		printf("[LD-MCCA] DT_SYMTAB: 0x%08x\n", dt_symtab);
		printf("[LD-MCCA] DT_JMPREL: 0x%08x (Size: %d bytes)\n", dt_jmprel, dt_pltrelsz);
		printf("[LD-MCCA] DT_REL: 0x%08x (Size: %d bytes)\n", dt_rel, dt_relsz);
		
		// The image is normally mapped at its link addresses, so the bias is zero unless the auxv says otherwise
		unsigned int main_bias = 0;
		bool phdr_mapped = false;
		for (unsigned int i = 0; at_phdr && i < at_phnum; i++) {
			ELF_PHT_t* ph = (ELF_PHT_t*)(at_phdr + i * at_phent);
			if (ph->p_type == PT_LOAD && at_phdr >= ph->p_vaddr && at_phdr < ph->p_vaddr + ph->p_memsz) {
				phdr_mapped = true;
				break;
			}
		}
		if (at_phdr && !phdr_mapped) {
			printf("[LD-MCCA] Warning: AT_PHDR is outside every PT_LOAD, relocation addresses may be wrong\n");
		}
		
		dyn = (Elf32_Dyn*)dynamic_addr;
		while (dyn->d_tag != DT_NULL) {
			if (dyn->d_tag == DT_NEEDED) {
				const char* lib_name = (const char*)(dt_strtab + dyn->d_un.d_val);
				printf("[LD-MCCA] Raw NEEDED library path from ELF: %s\n", lib_name);
				
				// The linker might have embedded the full host path (e.g., accmlib/sysroot/...)
				// We truncate it to extract just the base filename so we can search for it in the VFS.
				const char* base_name = strrchr(lib_name, '/');
				if (base_name) {
					base_name++; // Skip the slash
				} else {
					base_name = lib_name;
				}
				
				printf("[LD-MCCA] Truncated to base name: %s\n", base_name);
				
				// Try to open it from the system library path
				char target_path[256];
				// Prefer the virtual root injected by the kernel, the probes below stay as fallback
				const char* vroot = getenv("VRPATH");
				if (vroot && *vroot) sprintf(target_path, "%s/lib/%s", vroot, base_name);
				else sprintf(target_path, "/mnt/ide2.0/%s/lib/%s", runtime_arch_dir, base_name);
				
				int lib_fd = open(target_path, O_RDONLY);
				if (lib_fd < 0) {
					// Fallback to /lib/
					sprintf(target_path, "/mnt/ahci1.0/%s/lib/%s", runtime_arch_dir, base_name);
					lib_fd = open(target_path, O_RDONLY);
				}
				if (lib_fd < 0) {
					// Fallback to /lib/
					sprintf(target_path, "/lib/%s", base_name);
					lib_fd = open(target_path, O_RDONLY);
				}
				
				if (lib_fd >= 0) {
					printf("[LD-MCCA] Successfully opened library at %s!\n", target_path);
					// Hold the inode stable while parsing, mapping, and relocating this image.
					LibraryMappingOwner file_guard;
					file_guard.address = mmap(nullptr, LibraryPageSize,
						PROT_READ | PROT_EXEC, MAP_PRIVATE, lib_fd, 0);
					if (file_guard.address == MAP_FAILED) {
						file_guard.address = nullptr;
						close(lib_fd);
						return 1;
					}
					file_guard.length = LibraryPageSize;
					const off_t file_length = lseek(lib_fd, 0, SEEK_END);
					if (file_length < off_t(sizeof(ELF_Header_t))) {
						close(lib_fd);
						return 1;
					}
					const unsigned int file_size = unsigned(file_length);
					ELF_Header_t lib_header = {};
					if (!ReadLibraryBytes(lib_fd, 0, &lib_header,
						sizeof(lib_header), file_size)) { close(lib_fd); return 1; }
					if (lib_header.MagicNumber[0] == 0x7F &&
						lib_header.MagicNumber[1] == 'E' &&
						lib_header.MagicNumber[2] == 'L' &&
						lib_header.MagicNumber[3] == 'F') {
						printf("[LD-MCCA] Verified ELF signature for %s!\n", base_name);
						if (lib_header.Bitmode != ELF32 ||
							lib_header.Endianness != ELFDATA2LSB ||
							lib_header.ELFVersion != EV_CURRENT ||
							lib_header.e_version != EV_CURRENT ||
							lib_header.e_type != ET_DYN ||
							lib_header.e_machine != EM_386 ||
							lib_header.e_ehsize != sizeof(lib_header) ||
							!lib_header.e_phnum ||
							lib_header.e_phentsize != sizeof(ELF_PHT_t) ||
							lib_header.e_phoff > file_size ||
							lib_header.e_phnum > (file_size - lib_header.e_phoff) /
								sizeof(ELF_PHT_t)) {
							plogerro("[LD-MCCA] invalid library header: %s", target_path);
							close(lib_fd);
							return 1;
						}
						
						// The library exports far more symbols than any fixed bound can guess, so read the real count from .dynsym
						unsigned int libc_sym_count = 1;
						ELF_SHT_t* sh_table = nullptr;
						if (lib_header.e_shnum &&
							(lib_header.e_shentsize != sizeof(ELF_SHT_t) ||
							lib_header.e_shoff > file_size ||
							lib_header.e_shnum > (file_size - lib_header.e_shoff) /
								sizeof(ELF_SHT_t))) { close(lib_fd); return 1; }
						if (lib_header.e_shnum) {
							sh_table = new ELF_SHT_t[lib_header.e_shnum];
							if (!sh_table || !ReadLibraryBytes(lib_fd, lib_header.e_shoff,
								sh_table, lib_header.e_shnum * sizeof(ELF_SHT_t),
								file_size)) { delete[] sh_table; close(lib_fd); return 1; }
							for (int k = 0; k < lib_header.e_shnum; k++) {
								if (sh_table[k].sh_type == SHT_DYNSYM) {
									if (sh_table[k].sh_entsize != sizeof(Elf32_Sym) ||
										sh_table[k].sh_size % sizeof(Elf32_Sym) ||
										sh_table[k].sh_offset > file_size ||
										sh_table[k].sh_size > file_size - sh_table[k].sh_offset) {
										delete[] sh_table;
										close(lib_fd);
										return 1;
									}
									libc_sym_count = sh_table[k].sh_size / sh_table[k].sh_entsize;
									break;
								}
							}
						}
						printf("[LD-MCCA] Library exports %u dynamic symbols\n", libc_sym_count);
						
						ELF_PHT_t* ph_table = new ELF_PHT_t[lib_header.e_phnum];
						if (!ph_table) {
							plogerro("[LD-MCCA] no memory for the program headers");
							delete[] sh_table;
							close(lib_fd);
							return 1;
						}
						if (!ReadLibraryBytes(lib_fd, lib_header.e_phoff, ph_table,
							lib_header.e_phnum * sizeof(ELF_PHT_t), file_size)) {
							delete[] ph_table;
							delete[] sh_table;
							close(lib_fd);
							return 1;
						}
						LibraryLoadPlan plan;
						if (!PlanLibraryLoads(lib_fd, file_size, lib_header,
							ph_table, plan)) {
							plogerro("[LD-MCCA] invalid library ELF: %s", target_path);
							delete[] ph_table;
							delete[] sh_table;
							close(lib_fd);
							return 1;
						}
						char* lib_base = nullptr;
						LibraryMappingOwner library_mapping;
						const bool mapped = MapLibraryPages(lib_fd,
							file_size, ph_table, lib_header.e_phnum, plan, lib_base,
							library_mapping);
						if (!mapped) {
							plogerro("[LD-MCCA] failed to map library pages: %s", target_path);
							delete[] ph_table;
							delete[] sh_table;
							close(lib_fd);
							return 1;
						}
						bool executable_file_page = false;
						for (unsigned int page = 0;
							page < (plan.end_page - plan.first_page) / LibraryPageSize;
							page++) {
							if (plan.pages[page].kind == LibraryPageKind::File &&
								(plan.pages[page].flags & PF_X)) {
								executable_file_page = true;
								break;
							}
						}
						// Every file VMA owns a reference after MAP_FIXED succeeds.
						close(lib_fd);
						if (!executable_file_page) {
							// Keep the guard until exit when no code file VMA can own the pin.
							file_guard.Release();
						}
						
						// Parse libc's .dynamic to find its SYMTAB, STRTAB, and REL
						unsigned int libc_strtab = 0;
						unsigned int libc_symtab = 0;
						unsigned int libc_rel = 0;
						unsigned int libc_relsz = 0;
						unsigned int libc_jmprel = 0;
						unsigned int libc_pltrelsz = 0;
						
						for (int k = 0; k < lib_header.e_phnum; k++) {
							if (ph_table[k].p_type == PT_DYNAMIC) {
								Elf32_Dyn* libc_dyn = (Elf32_Dyn*)(lib_base + ph_table[k].p_vaddr);
								while (libc_dyn->d_tag != DT_NULL) {
									if (libc_dyn->d_tag == DT_STRTAB) libc_strtab = (unsigned int)lib_base + libc_dyn->d_un.d_ptr;
									if (libc_dyn->d_tag == DT_SYMTAB) libc_symtab = (unsigned int)lib_base + libc_dyn->d_un.d_ptr;
									if (libc_dyn->d_tag == DT_REL) libc_rel = (unsigned int)lib_base + libc_dyn->d_un.d_ptr;
									if (libc_dyn->d_tag == DT_RELSZ) libc_relsz = libc_dyn->d_un.d_val;
									if (libc_dyn->d_tag == DT_JMPREL) libc_jmprel = (unsigned int)lib_base + libc_dyn->d_un.d_ptr;
									if (libc_dyn->d_tag == DT_PLTRELSZ) libc_pltrelsz = libc_dyn->d_un.d_val;
									libc_dyn++;
								}
								break;
							}
						}
						
						printf("[LD-MCCA] libc SYMTAB: 0x%08x, STRTAB: 0x%08x\n", libc_symtab, libc_strtab);
						
						// Apply only relocations that the validated library plan accepted.
						auto process_relocs = [&](unsigned int rel_addr, unsigned int rel_sz) -> bool {
							if (!rel_sz) return true;
							if (!rel_addr || rel_sz % sizeof(Elf32_Rel)) return false;
							int rel_count = rel_sz / sizeof(Elf32_Rel);
							Elf32_Rel* rel_table = (Elf32_Rel*)rel_addr;
							int processed = 0;
							Elf32_Sym* libc_syms = (Elf32_Sym*)libc_symtab;
							
							for (int i = 0; i < rel_count; i++) {
								int rtype = ELF32_R_TYPE(rel_table[i].r_info);
								const unsigned int sym_idx = ELF32_R_SYM(rel_table[i].r_info);
								if (rel_table[i].r_offset > plan.end_page - sizeof(unsigned int) ||
									rel_table[i].r_offset < plan.first_page ||
									(rtype != R_386_RELATIVE &&
										(!libc_syms || sym_idx >= libc_sym_count))) return false;
								const auto& target_page = plan.pages[
									(rel_table[i].r_offset - plan.first_page) / LibraryPageSize];
								if (!(target_page.flags & PF_W) ||
									target_page.kind == LibraryPageKind::Hole ||
									(rel_table[i].r_offset & (LibraryPageSize - 1)) >
										LibraryPageSize - sizeof(unsigned int)) return false;
								unsigned int* ptr = (unsigned int*)((unsigned int)lib_base + rel_table[i].r_offset);
								
								if (rtype == R_386_RELATIVE) { // 8
									*ptr += (unsigned int)lib_base;
									processed++;
								} 
								else if (rtype == R_386_GLOB_DAT || rtype == R_386_JUMP_SLOT || rtype == R_386_32) {
									unsigned int resolved_addr = 0;
									
									if (libc_syms[sym_idx].st_shndx != SHN_UNDEF) {
										resolved_addr = (unsigned int)lib_base + libc_syms[sym_idx].st_value;
									}
									
									if (resolved_addr) {
										if (rtype == R_386_32) {
											*ptr += resolved_addr;
										} else {
											*ptr = resolved_addr;
										}
										processed++;
									}
								} else return false;
							}
							printf("[LD-MCCA] Processed %d internal relocations for libc.so\n", processed);
							return true;
						};
						
						// Perform internal relocations for libc.so itself
						if (!process_relocs(libc_rel, libc_relsz) ||
							!process_relocs(libc_jmprel, libc_pltrelsz) ||
							!ProtectLibraryRELRO(ph_table, lib_header.e_phnum,
								plan, lib_base)) {
							plogerro("[LD-MCCA] library relocation or RELRO failed: %s", target_path);
							delete[] ph_table;
							delete[] sh_table;
							return 1;
						}
						
						// Resolve one imported name against libc's dynamic symbol table
						Elf32_Sym* libc_syms = (Elf32_Sym*)libc_symtab;
						auto resolve_name = [&](const char* sym_name) -> unsigned int {
							if (!sym_name || !libc_syms || !libc_strtab) return 0;
							for (unsigned int s = 1; s < libc_sym_count; s++) {
								if (libc_syms[s].st_name == 0) continue; // Skip unnamed
								const char* l_name = (const char*)(libc_strtab + libc_syms[s].st_name);
								if (libc_syms[s].st_shndx != SHN_UNDEF &&
									strcmp(sym_name, l_name) == 0) {
									return (unsigned int)lib_base + libc_syms[s].st_value;
								}
							}
							return 0;
						};
						
						// .rel.dyn and .rel.plt of the main executable both land here
						auto apply_main_relocs = [&](unsigned int rel_addr, unsigned int rel_sz, const char* what) -> bool {
							if (!rel_sz) return true;
							if (!rel_addr || rel_sz % sizeof(Elf32_Rel)) return false;
							Elf32_Rel* rel_table = (Elf32_Rel*)rel_addr;
							Elf32_Sym* main_symtab = (Elf32_Sym*)dt_symtab;
							const char* main_strtab = (const char*)dt_strtab;
							int rel_count = rel_sz / sizeof(Elf32_Rel);
							int applied = 0, failed = 0;
							printf("[LD-MCCA] Processing %d %s relocations...\n", rel_count, what);
							for (int i = 0; i < rel_count; i++) {
								int rtype = ELF32_R_TYPE(rel_table[i].r_info);
								int sym_idx = ELF32_R_SYM(rel_table[i].r_info);
								unsigned int* field = (unsigned int*)(rel_table[i].r_offset + main_bias);
								if (rtype == R_386_RELATIVE) {
									*field += main_bias;
									applied++;
									continue;
								}
								const char* sym_name = (main_strtab && main_symtab) ? (main_strtab + main_symtab[sym_idx].st_name) : "";
								unsigned int target = resolve_name(sym_name);
								if (!target) {
									failed++;
									plogerro("[LD-MCCA] symbol %s not found in libc", sym_name);
									continue;
								}
								if (rtype == R_386_COPY) {
									memcpy(field, (const void*)target, main_symtab[sym_idx].st_size);
								} else if (rtype == R_386_PC32) {
									*field = target + *field - (unsigned int)field;
								} else if (rtype == R_386_32) {
									*field += target;
								} else if (rtype == R_386_GLOB_DAT || rtype == R_386_JUMP_SLOT) {
									*field = target;
								} else {
									failed++;
									plogerro("[LD-MCCA] %s has unsupported relocation type %d", sym_name, rtype);
									continue;
								}
								printf("[LD-MCCA] Relocating %s to 0x%08x\n", sym_name, target);
								applied++;
							}
							printf("[LD-MCCA] Applied %d %s relocations, %d failed\n", applied, what, failed);
							return failed == 0;
						};
						
						if (!apply_main_relocs(dt_rel, dt_relsz, "REL") ||
							!apply_main_relocs(dt_jmprel, dt_pltrelsz, "PLT")) {
							delete[] ph_table;
							delete[] sh_table;
							return 1;
						}
						library_mapping.Release();
						
						delete[] ph_table;
						delete[] sh_table;
					} else {
						plogerro("[LD-MCCA] invalid ELF signature in %s", target_path);
						close(lib_fd);
						return 1;
					}
				} else {
					plogerro("[LD-MCCA] failed to open library %s", base_name);
					return 1;
				}
			}
			dyn++;
		}
	}
	
	printf("[LD-MCCA] Transferring control to AT_ENTRY (0x%08x). Hold on tight!\n", at_entry);
	printf("========================================\n\n");
	
	unsigned int original_esp = (unsigned int)argv - 4;
	asm volatile (
		"mov %0, %%esp \n"
		"jmp *%1 \n"
		: : "r"(original_esp), "r"(at_entry)
	);
	
	// Should never reach here
	return 0;
}
