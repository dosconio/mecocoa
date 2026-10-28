// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include "../include/filesys.hpp"

#if CONFIG_ENABLE_MMU
namespace uni {
	extern Mutex vfs_lock;
	bool PinExecutableImage(ProcessBlock* process, vfs_file* file,
		uint64 expected_version);
	void UnpinExecutableImage(ProcessBlock* process);
}

struct ExecutablePinScope {
	ProcessBlock* process = nullptr;
	~ExecutablePinScope() {
		if (!process) return;
		{
			MutexLocal guard(&process->mutex_mapping);
			process->ReleaseAllVmas();
			process->ReleaseLoadslices();
		}
		uni::UnpinExecutableImage(process);
	}
	void Keep() { process = nullptr; }
};

// Only a complete, aligned PT_LOAD page can use the shared file cache.
static bool FullFileLoadPage(const ELF_PHT_t* headers, stduint count,
	stduint segment, stduint page, stduint load_bias) {
	const auto& ph = headers[segment];
	const stduint begin = ph.p_vaddr + load_bias;
	if (page < begin || ph.p_filesz < PAGE_SIZE ||
		page - begin > ph.p_filesz - PAGE_SIZE ||
		((ph.p_offset + page - begin) & (PAGE_SIZE - 1))) return false;
	for (stduint i = 0; i < count; i++) {
		if (i == segment || headers[i].p_type != PT_LOAD ||
			!headers[i].p_memsz) continue;
		const stduint other_begin = headers[i].p_vaddr + load_bias;
		const stduint other_end = other_begin + headers[i].p_memsz;
		if (page < other_end && other_begin < page + PAGE_SIZE) return false;
	}
	return true;
}

static bool PlanMainFileLoads(BlockTrait* source, const ELF_Header_t& header,
	stduint file_size, stduint load_bias, byte* buffer,
	ELF_PHT_t* headers) {
	if ((header.e_type != ET_EXEC && header.e_type != ET_DYN) ||
		!header.e_phnum || header.e_phentsize != sizeof(ELF_PHT_t) ||
		header.e_phoff > file_size ||
		stduint(header.e_phnum) >
			(file_size - header.e_phoff) / sizeof(ELF_PHT_t)) return false;
	bool has_load = false;
	bool entry_in_segment = false;
	#if _MCCA == 0x8664
	const stduint user_end = 0x0000800000000000ull;
	#elif _MCCA == 0x1064
	const stduint user_end = 0x4000000000ull;
	#else
	const stduint user_end = 0x80000000;
	#endif
	for (stduint i = 0; i < header.e_phnum; i++) {
		if (source->Read(header.e_phoff + i * sizeof(ELF_PHT_t),
			&headers[i], sizeof(ELF_PHT_t), buffer) != sizeof(ELF_PHT_t))
			return false;
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		has_load = true;
		if (ph.p_filesz > ph.p_memsz || ph.p_offset > file_size ||
			ph.p_filesz > file_size - ph.p_offset ||
			((ph.p_offset ^ ph.p_vaddr) & (PAGE_SIZE - 1)) ||
			(ph.p_align > 1 &&
				((ph.p_align & (ph.p_align - 1)) ||
				((ph.p_vaddr - ph.p_offset) & (ph.p_align - 1)))) ||
			ph.p_vaddr > ~_IMM0 - load_bias) return false;
		const stduint begin = ph.p_vaddr + load_bias;
		if (begin < 0x10000 || begin >= user_end ||
			ph.p_memsz > user_end - begin ||
			ph.p_memsz > ~_IMM0 - (PAGE_SIZE - 1) - begin)
			return false;
		if (begin < 0x1000 + HIGHER_STACK_SIZE &&
			begin + ph.p_memsz > 0x1000) return false;
		const stduint entry = header.e_entry + load_bias;
		if ((ph.p_flags & PF_X) && entry >= begin &&
			entry - begin < ph.p_memsz) entry_in_segment = true;
	}
	return has_load && header.e_entry <= ~_IMM0 - load_bias &&
		entry_in_segment;
}

static bool MapMainFileLoads(ProcessBlock* pb, BlockTrait* source,
	uni::vfs_file* file, const ELF_PHT_t* headers, stduint count,
	stduint load_bias, byte* buffer, stduint& load_slice_p,
	stduint& max_seg_end) {
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		const stduint begin = ph.p_vaddr + load_bias;
		const stduint end = begin + ph.p_memsz;
		const stduint file_end = begin + ph.p_filesz;
		const bool executable = !!(ph.p_flags & PF_X);
		const bool writable = !!(ph.p_flags & PF_W);
		stduint flags = PGPROP_present | PGPROP_user_access;
		if (writable) flags |= PGPROP_writable;
		#if _MCCA != 0x8632
		if (!executable) flags |= PGPROP_nonexecutable;
		#endif
		for (stduint page = begin & ~_IMM(PAGE_SIZE - 1);
			page < end; page += PAGE_SIZE) {
			if (FullFileLoadPage(headers, count, i, page, load_bias)) {
				const stduint file_offset = ph.p_offset + page - begin;
				stduint run_end = page + PAGE_SIZE;
				while (run_end < end && FullFileLoadPage(headers, count,
					i, run_end, load_bias)) run_end += PAGE_SIZE;
				auto* backing = new uni::vfs_file(*file);
				if (!backing) return false;
				VirtualMemoryArea vma(page, run_end, flags);
				vma.vm_type = VirtualMemoryAreaType::File;
				vma.vm_mode = VirtualMemoryAreaMode::Private;
				vma.vfile = backing;
				vma.file_offset = file_offset;
				bool inserted = false;
				{
					// The new process is unpublished; Exet already holds mutex_mapping.
					SpinlockLocal vma_guard(&pb->vma_lock);
					inserted = pb->InsertVmaSorted(vma);
				}
				if (!inserted) {
					uni::Filesys::Close(backing);
					return false;
				}
				page = run_end - PAGE_SIZE;
				continue;
			}
			const stduint chunk_begin = maxof(begin, page);
			const stduint chunk_end = minof(end, page + PAGE_SIZE);
			const stduint chunk_file_size = chunk_begin < file_end ?
				minof(chunk_end, file_end) - chunk_begin : 0;
			if (!Taskman::CreateELF_Carry((char*)chunk_begin,
				chunk_end - chunk_begin, source,
				ph.p_offset + chunk_begin - begin, chunk_file_size,
				pb->paging, buffer, executable, writable, true)) return false;
		}
		if (load_slice_p >= numsof(pb->load_slices)) return false;
		pb->load_slices[load_slice_p].address = begin;
		pb->load_slices[load_slice_p].length = ph.p_memsz;
		load_slice_p++;
		max_seg_end = maxof(max_seg_end, end);
	}
	return true;
}
#endif
#if defined(_ARCH_ARM_ProfileM)
#include <cpp/Device/Cache>
#endif
#if !CONFIG_ENABLE_MMU
#define MemCopyP(a,b,c,d,e) _IMM((byte*)MemCopyN(a,c,e)-(byte*)a)
#define StrCopyP(a,b,c,d,e) _IMM((byte*)StrCopyN(a,c,e)-(byte*)a)
#endif

#if CONFIG_ENABLE_MMU
static void RollbackELFPages(Paging& paging, const stduint* addresses,
	stduint count) {
	while (count) {
		const stduint address = addresses[--count];
		void* physical = paging[address];
		if (physical == (void*)~_IMM0) continue;
		if (!paging.Unmap(address, PAGE_SIZE)) {
			plogerro("Failed to roll back ELF page at VA=%[x]", address);
			continue;
		}
		if (!Memory::ReleaseManagedPageMapping(_IMM(physical)))
			plogerro("Missing managed ELF page at physical address %[x]",
				physical);
	}
}
#endif

bool Taskman::CreateELF_Carry(char* vaddr, stduint mem_length, BlockTrait* source, stduint file_offset, stduint file_size, Paging& pg, byte* buffer, bool executable, bool writable, bool user) {
	// if page !exist, map it; write it.
	stduint compensation = _IMM(vaddr) & 0xFFF;
	stduint v_start1 = _IMM(vaddr) & ~_IMM(0xFFF);
	if (_IMM(vaddr) < 0x10000) {
		plogerro("%s: vaddr is too low", __FUNCIDEN__);
	}
	#if _MCCA == 0x8632 || _MCCA == 0x1032
	if (_IMM(vaddr) >= 0x80000000) {
		plogerro("Taskman::CreateELF_Carry");
	}
	#elif _MCCA == 0x8664
	if (_IMM(vaddr) >= 0xFFFFC0000000ull) {
		plogerro("Taskman::CreateELF_Carry");
	}
	#elif _MCCA == 0x1064
    if (_IMM(vaddr) >= 0x4000000000ull) { 
        plogerro("Taskman::CreateELF_Carry: vaddr out of RV64 Sv39 user space");
    }
	#endif

	#if CONFIG_ENABLE_MMU
	// ploginfo("Taskman::CreateELF_Carry(%[x], %[x], %[x])  pg(%[x])", vaddr, length, phy_src, pg);
	const stduint page_count = (mem_length + compensation + 0xFFF) / 0x1000;
	auto allocated_pages = uni::make_unique<stduint[]>(page_count);
	if (page_count && !allocated_pages) return false;
	stduint allocated_page_count = 0;
	stduint bytes_read = 0; 
	for0(i, page_count) {
		auto page_entry = pg.getEntry(_IMM(v_start1));
		stduint phy;
		
		if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
			phy = _IMM(mempool.allocate(0x1000, 12));
			if (!phy || phy == ~_IMM0) {
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
			MemSet((void*)phy, 0, 0x1000);
			if (!Memory::AdoptManagedPage(phy, ManagedPageSource::ELF)) {
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
			stduint pgprop = PGPROP_present;
			if (writable) pgprop |= PGPROP_writable;
			if (user) pgprop |= PGPROP_user_access;
			#if _MCCA != 0x8632
			if (!executable) pgprop |= PGPROP_nonexecutable;
			#endif
			if (!pg.Map(v_start1, phy, 0x1000, PAGESIZE_4KB, pgprop)) {
				Memory::ReleaseManagedPageMapping(phy);
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
			allocated_pages[allocated_page_count++] = v_start1;
			page_entry = pg.getEntry(_IMM(v_start1));
			if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
				plogerro("Mapping failed %[x] -> %[x]", v_start1, phy);
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
		}
		else phy = page_entry->getAddress(0);

		stduint chunk_size = 0x1000 - compensation;
		if (chunk_size > mem_length - bytes_read) {
			chunk_size = mem_length - bytes_read; 
		}

		stduint phy_dest = phy + compensation;
		void* kdest = (void*)(phy_dest);

		// xDATA or BSS 
		if (bytes_read >= file_size) {
			MemSet(kdest, 0, chunk_size);
		} 
		else {
			stduint copy_size = chunk_size;
			if (bytes_read + chunk_size > file_size) {
				copy_size = file_size - bytes_read;
				MemSet((void*)(phy_dest + copy_size), 0, chunk_size - copy_size);
			}
			auto ret = source->Read(file_offset + bytes_read, kdest, copy_size, buffer);
			if (ret != copy_size) {
				plogwarn("%s %u: Read failed, %u != %u", __FUNCIDEN__, __LINE__, ret, copy_size);
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
		}

		bytes_read += chunk_size;
		v_start1 += 0x1000;
		compensation = 0; 
	}
	return true;
	#endif
	return false;
}

bool _Taskman_Relocate_PIE(BlockTrait* source, const ELF_Header_t& header,
	stduint load_bias, ProcessBlock* pb, bool mapping_lock_held,
	byte* block_buffer) {
	if (!load_bias) return true;
	#if _MCCA != 0x8632 && _MCCA != 0x8664
	plogerro("%s: ET_DYN (PIE) is not relocatable on this architecture (_MCCA=%[x])", __FUNCIDEN__, (stduint)_MCCA);
	return false;
	#else
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type == PT_DYNAMIC && ph.p_filesz) {
			#if _MCCA == 0x8632
			stduint rel_vaddr = 0, rel_sz = 0, rel_ent = sizeof(Elf32_Rel);
			stduint num_dyn = ph.p_filesz / sizeof(Elf32_Dyn);
			for0(j, num_dyn) {
				Elf32_Dyn dyn;
				source->Read(ph.p_offset + j * sizeof(Elf32_Dyn), &dyn, sizeof(dyn), block_buffer);
				if (dyn.d_tag == DT_NULL) break;
				if (dyn.d_tag == DT_REL) rel_vaddr = dyn.d_un.d_ptr;
				else if (dyn.d_tag == DT_RELSZ) rel_sz = dyn.d_un.d_val;
				else if (dyn.d_tag == DT_RELENT) rel_ent = dyn.d_un.d_val;
			}
			if (rel_vaddr && rel_sz && rel_ent) {
				stduint rel_file_off = 0;
				bool rel_found = false;
				for0(k, header.e_phnum) {
					struct ELF_PHT_t lph;
					source->Read(header.e_phoff + k * header.e_phentsize, &lph, sizeof(lph), block_buffer);
					if (lph.p_type == PT_LOAD && rel_vaddr >= lph.p_vaddr && rel_vaddr < lph.p_vaddr + lph.p_filesz) {
						rel_file_off = lph.p_offset + (rel_vaddr - lph.p_vaddr);
						rel_found = true;
						break;
					}
				}
				if (!rel_found) {
					plogerro("%s: DT_REL (vaddr=%[x]) is not covered by any PT_LOAD", __FUNCIDEN__, rel_vaddr);
					return false;
				}
				stduint rel_count = rel_sz / rel_ent;
				for0(r, rel_count) {
					Elf32_Rel rel;
					source->Read(rel_file_off + r * sizeof(Elf32_Rel), &rel, sizeof(rel), block_buffer);
					if (ELF32_R_TYPE(rel.r_info) == R_386_RELATIVE) {
						stduint target_vaddr = rel.r_offset + load_bias;
						if (!PrepareUserBuffer(pb, target_vaddr,
							sizeof(uint32), true, mapping_lock_held)) return false;
						void* phy_page = pb->paging[target_vaddr & ~_IMM(0xFFF)];
						if (phy_page != (void*)~_IMM0) {
							uint32* val_ptr = (uint32*)((byte*)phy_page + (target_vaddr & _IMM(0xFFF)));
							*val_ptr += (uint32)load_bias;
						}
					}
				}
			}
			#elif _MCCA == 0x8664
			stduint rela_vaddr = 0, rela_sz = 0, rela_ent = sizeof(Elf64_Rela);
			stduint num_dyn = ph.p_filesz / sizeof(Elf64_Dyn);
			for0(j, num_dyn) {
				Elf64_Dyn dyn;
				source->Read(ph.p_offset + j * sizeof(Elf64_Dyn), &dyn, sizeof(dyn), block_buffer);
				if (dyn.d_tag == DT_NULL) break;
				if (dyn.d_tag == DT_RELA) rela_vaddr = dyn.d_un.d_ptr;
				else if (dyn.d_tag == DT_RELASZ) rela_sz = dyn.d_un.d_val;
				else if (dyn.d_tag == DT_RELAENT) rela_ent = dyn.d_un.d_val;
			}
			if (rela_vaddr && rela_sz && rela_ent) {
				stduint rela_file_off = 0;
				bool rela_found = false;
				for0(k, header.e_phnum) {
					struct ELF_PHT_t lph;
					source->Read(header.e_phoff + k * header.e_phentsize, &lph, sizeof(lph), block_buffer);
					if (lph.p_type == PT_LOAD && rela_vaddr >= lph.p_vaddr && rela_vaddr < lph.p_vaddr + lph.p_filesz) {
						rela_file_off = lph.p_offset + (rela_vaddr - lph.p_vaddr);
						rela_found = true;
						break;
					}
				}
				if (!rela_found) {
					plogerro("%s: DT_RELA (vaddr=%[x]) is not covered by any PT_LOAD", __FUNCIDEN__, rela_vaddr);
					return false;
				}
				stduint rela_count = rela_sz / rela_ent;
				for0(r, rela_count) {
					Elf64_Rela rela;
					source->Read(rela_file_off + r * sizeof(Elf64_Rela), &rela, sizeof(rela), block_buffer);
					if (ELF64_R_TYPE(rela.r_info) == R_X86_64_RELATIVE) {
						stduint target_vaddr = rela.r_offset + load_bias;
						if (!PrepareUserBuffer(pb, target_vaddr,
							sizeof(uint64), true, mapping_lock_held)) return false;
						void* phy_page = pb->paging[target_vaddr & ~_IMM(0xFFF)];
						if (phy_page != (void*)~_IMM0) {
							uint64* val_ptr = (uint64*)((byte*)phy_page + (target_vaddr & _IMM(0xFFF)));
							*val_ptr = (uint64)(rela.r_addend + load_bias);
						}
					}
				}
			}
			#endif
			break;
		}
	}
	return true;
	#endif
}

#if !CONFIG_ENABLE_MMU

// NoMMU: the image is a flat copy, so load_bias is the physical address of vaddr 0 and a relocation is an in-place add
bool _Taskman_Relocate_PIE_Flat(BlockTrait* source, const ELF_Header_t& header, stduint load_bias, byte* block_buffer) {
	if (!load_bias) return true;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type != PT_DYNAMIC || !ph.p_filesz) continue;
		stduint rel_vaddr = 0, rel_sz = 0, rel_ent = sizeof(Elf32_Rel), rela_vaddr = 0;
		const stduint num_dyn = ph.p_filesz / sizeof(Elf32_Dyn);
		for0(j, num_dyn) {
			Elf32_Dyn dyn;
			source->Read(ph.p_offset + j * sizeof(Elf32_Dyn), &dyn, sizeof(dyn), block_buffer);
			if (dyn.d_tag == DT_NULL) break;
			if (dyn.d_tag == DT_REL) rel_vaddr = dyn.d_un.d_ptr;
			else if (dyn.d_tag == DT_RELSZ) rel_sz = dyn.d_un.d_val;
			else if (dyn.d_tag == DT_RELENT) rel_ent = dyn.d_un.d_val;
			else if (dyn.d_tag == DT_RELA) rela_vaddr = dyn.d_un.d_ptr;
		}
		if (!rel_vaddr) {
			// 32-bit ARM relocates with REL: a RELA table means the wrong target was built
			if (rela_vaddr) {
				plogerro("%s: DT_RELA is not supported, expected DT_REL", __FUNCIDEN__);
				return false;
			}
			continue;
		}
		if (!rel_sz || !rel_ent) continue;
		stduint rel_file_off = 0;
		bool rel_found = false;
		for0(k, header.e_phnum) {
			struct ELF_PHT_t lph;
			source->Read(header.e_phoff + k * header.e_phentsize, &lph, sizeof(lph), block_buffer);
			if (lph.p_type == PT_LOAD && rel_vaddr >= lph.p_vaddr && rel_vaddr < lph.p_vaddr + lph.p_filesz) {
				rel_file_off = lph.p_offset + (rel_vaddr - lph.p_vaddr);
				rel_found = true;
				break;
			}
		}
		if (!rel_found) {
			plogerro("%s: DT_REL (vaddr=%[x]) is not covered by any PT_LOAD", __FUNCIDEN__, rel_vaddr);
			return false;
		}
		const stduint rel_count = rel_sz / rel_ent;
		stduint image_span = 0;
		for0(k, header.e_phnum) {
			struct ELF_PHT_t lph;
			source->Read(header.e_phoff + k * header.e_phentsize, &lph, sizeof(lph), block_buffer);
			if (lph.p_type == PT_LOAD && lph.p_vaddr + lph.p_memsz > image_span) image_span = lph.p_vaddr + lph.p_memsz;
		}
		for0(r, rel_count) {
			Elf32_Rel rel;
			source->Read(rel_file_off + r * sizeof(Elf32_Rel), &rel, sizeof(rel), block_buffer);
			const stduint rel_type = ELF32_R_TYPE(rel.r_info);
			if (!rel_type) continue;
			if (rel.r_offset + 4 > image_span) {
				plogerro("%s: reloc %u at %[x] is outside the %u byte image", __FUNCIDEN__, rel_type, rel.r_offset, image_span);
				return false;
			}
			#ifdef ELF_RELOC_RELATIVE
			if (rel_type == ELF_RELOC_RELATIVE || rel_type == ELF_RELOC_ABS32) {
				*(uint32*)(load_bias + rel.r_offset) += (uint32)load_bias;
				// ploginfo("[ELF] reloc %[x] += %[x]", rel.r_offset, load_bias);
				continue;
			}
			#endif
			plogerro("%s: unsupported reloc %u at %[x]", __FUNCIDEN__, rel_type, rel.r_offset);
			return false;
		}
	}
	return true;
}

#endif

// Resolve PT_INTERP: *out_interp set, or null when none is needed; false when not found
bool _Taskman_Resolve_Interp(BlockTrait* source, const ELF_Header_t& header, byte* block_buffer, vfs_dentry* cwd, vfs_dentry** out_interp) {
	*out_interp = nullptr;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type != PT_INTERP) continue;

		String interp_buf(String::Charset::Memory, 256);
		stduint read_sz = ph.p_filesz < 255 ? ph.p_filesz : 255;
		source->Read(ph.p_offset, interp_buf.reflect(), read_sz, block_buffer);

		String real_interp_path;
		const auto& runtime_root = Filesys::GetSystemRuntimeRootPath();
		if (runtime_root.getByteCount() > 0) {
			real_interp_path = String::newFormat("%s%s", runtime_root.reference(), interp_buf.reference());
		} else {
			real_interp_path = interp_buf;
		}

		ploginfo("%s: ELF interpreter mapped to: %s", __FUNCIDEN__, real_interp_path.reference());

		vfs_dentry* interp_d = Filesys::Index(real_interp_path.reference(), cwd);
		if (!interp_d) {
			plogerro("%s: Interpreter not found at %s", __FUNCIDEN__, real_interp_path.reference());
			return false;
		}
		ploginfo("%s: Interpreter FOUND and ready to load.", __FUNCIDEN__);
		*out_interp = interp_d;
		return true;
	}
	return true;
}

// Load the interpreter into pb->paging, register its load slices, report its entry point
bool _Taskman_Load_Interp(vfs_dentry* interp_d, ProcessBlock* pb, byte* block_buffer, stduint& load_slice_p, stduint* out_entry) {
	#if CONFIG_ENABLE_MMU
	// FileBlockBridge shares backend buffers with VFS operations.
	MutexLocal file_guard(&uni::vfs_lock);
	FileBlockBridge interp_device(interp_d->d_inode->i_sb->fs, interp_d->d_inode->internal_handler, interp_d->d_inode->i_size, 512);
	ELF_Header_t interp_header;
	interp_device.Read(0, &interp_header, sizeof(interp_header), block_buffer);
	*out_entry = _IMM(interp_header.e_entry);

	for0(i, interp_header.e_phnum) {
		struct ELF_PHT_t ph;
		interp_device.Read(interp_header.e_phoff + i * interp_header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;

		bool executable = !!(ph.p_flags & PF_X);
		bool writable = !!(ph.p_flags & PF_W);
		bool user = (pb->ring != RING_M);

		if (!Taskman::CreateELF_Carry((char*)ph.p_vaddr, ph.p_memsz, &interp_device, ph.p_offset, ph.p_filesz, pb->paging, block_buffer, executable, writable, user)) {
			plogerro("%s: interp segment load failed", __FUNCIDEN__);
			return false;
		}
		if (load_slice_p < numsof(pb->load_slices)) {
			pb->load_slices[load_slice_p].address = ph.p_vaddr;
			pb->load_slices[load_slice_p].length = ph.p_memsz;
			load_slice_p++;
		}
	}
	return true;
	#else
	return false;
	#endif
}

static ProcessBlock* CreateELFImage(BlockTrait* source,
	uni::vfs_file* image_file, byte ring, bool append) {
	#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
	#if CONFIG_ENABLE_MMU
	uint64 image_version = 0;
	stduint image_size = 0;
	if (image_file) {
		MutexLocal guard(&uni::vfs_lock);
		image_version = image_file->f_inode->content_version;
		image_size = image_file->f_inode->i_size;
	}
	#endif
	String block_buffer(String::Charset::Memory, 512);
	struct ELF_Header_t header;
	if (source->Read(0, &header, sizeof(header),
		(byte*)block_buffer.reflect()) != sizeof(header)) return nullptr;
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	const bool map_main_file = CONFIG_ENABLE_MMU && image_file && ring != RING_M;
	#if CONFIG_ENABLE_MMU
	auto program_headers = map_main_file ?
		uni::make_unique<ELF_PHT_t[]>(header.e_phnum) :
		uni::UniquePtr<ELF_PHT_t[]>();
	const stduint planned_bias = header.e_type == ET_DYN ? 0x400000 : 0;
	if (map_main_file && (!program_headers ||
		!PlanMainFileLoads(source, header, image_size,
			planned_bias, (byte*)block_buffer.reflect(),
			program_headers.get()))) return nullptr;
	#endif
	
	ProcessBlock* pb = Taskman::AllocateTask();
	if (!pb) return nullptr;
	#if CONFIG_ENABLE_MMU
	ExecutablePinScope pin_scope;
	if (map_main_file) {
		if (!uni::PinExecutableImage(pb, image_file,
			image_version)) {
			ProcessBlock::Release(pb);
			return nullptr;
		}
		pin_scope.process = pb;
	}
	#endif
	pb->ring = ring;
	pb->parent_id = Task_Kernel;
	*pb->focus_tty.Lock() = nullptr;
	
	auto tb = Taskman::AllocateThread();
	pb->main_thread = tb;
	pb->thread_list_head = tb;
	tb->parent_process = pb;

	tb->stack_size = HIGHER_STACK_SIZE;
	auto stack_norm = (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB);
	auto stack_ring = ring != RING_M ? (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB) : stack_norm;
	tb->stack_lineaddr = (byte*)0x1000;
	tb->stack_levladdr = stack_ring;
	// [DIAG] Write stack canary at the bottom of the kernel stack
	*(stduint*)stack_ring = 0xDEADBEEF;

	// ---- CR3 Mapping ---- //
	stduint kernel_size = _TEMP 0x00400000;
	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.CR3 = Taskman::CreatePaging(pb, ring, _IMM(stack_norm));
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	tb->context.satp = Taskman::CreatePaging(pb, ring, _IMM(stack_norm));
	#endif

	stduint load_bias = (header.e_type == ET_DYN) ? 0x400000 : 0;
	tb->context.IP = _IMM(header.e_entry) + load_bias;

	vfs_dentry* interp_d = nullptr;
	if (!_Taskman_Resolve_Interp(source, header, (byte*)block_buffer.reflect(), nullptr, &interp_d)) {
		return nullptr;
	}

	stduint load_slice_p = 0;
	stduint max_seg_end = 0;
	stduint phdr_addr = 0;
	#if CONFIG_ENABLE_MMU
	if (map_main_file) {
		if (!MapMainFileLoads(pb, source, image_file,
			program_headers.get(), header.e_phnum, load_bias,
			(byte*)block_buffer.reflect(), load_slice_p, max_seg_end))
			return nullptr;
	}
	#endif
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		stduint ph_offset = header.e_phoff + header.e_phentsize * i;
		source->Read(ph_offset, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type == PT_DYNAMIC) {
			// PIE STATIC will have PT_DYNAMIC, do not abort
		}
		if (ph.p_type == PT_PHDR) phdr_addr = ph.p_vaddr + load_bias;
		if (ph.p_type == PT_LOAD && ph.p_offset == 0 && !phdr_addr) phdr_addr = ph.p_vaddr + load_bias + header.e_phoff;
		if (ph.p_type == PT_LOAD && ph.p_memsz && !map_main_file)
		{
			bool executable = !!(ph.p_flags & PF_X);
			bool writable = !!(ph.p_flags & PF_W);
			bool user = (ring != RING_M);
			if (!Taskman::CreateELF_Carry((char*)(ph.p_vaddr + load_bias), ph.p_memsz, source, ph.p_offset, ph.p_filesz, pb->paging, (byte*)block_buffer.reflect(), executable, writable, user)) {
				plogerro("%s: segment load failed (vaddr=%[x] memsz=%u)", __FUNCIDEN__, ph.p_vaddr + load_bias, ph.p_memsz);
				return nullptr;
			}
			if (load_slice_p < numsof(pb->load_slices)) {
				pb->load_slices[load_slice_p].address = ph.p_vaddr + load_bias;
				pb->load_slices[load_slice_p].length = ph.p_memsz;
				load_slice_p++;
			}
			else {
				plogwarn("[Taskman] CreateELF LoadSlice Overflow");
			}
			stduint seg_end = ph.p_vaddr + load_bias + ph.p_memsz;
			if (seg_end > max_seg_end) {
				max_seg_end = seg_end;
			}
		}
	}
	if (!_Taskman_Relocate_PIE(source, header, load_bias, pb, false,
		(byte*)block_buffer.reflect())) {
		plogerro("%s: PIE relocation failed", __FUNCIDEN__);
		return nullptr;
	}

	if (max_seg_end > 0) {
		pb->heapbtm = (max_seg_end + 0xFFF) & ~_IMM(0xFFF);
		pb->heapbtm += 0x10000;
		pb->heaptop = pb->heapbtm;
	} else {
		pb->heapbtm = 0x08000000;
		pb->heaptop = pb->heapbtm;
	}

	// ---- Stack and Gen.Regis ---- //
	const stduint stack_loc_top = _IMM(tb->stack_lineaddr) + tb->stack_size;
	const stduint initial_sp = Taskman::SetupStack(pb, pb, nullptr, nullptr,
		(stduint)header.e_entry + load_bias, phdr_addr, header.e_phnum, header.e_phentsize);

	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.RING = ring;
	tb->context.SP = initial_sp;
	// ploginfo("[elf] entry=%[x] sp=%[x] stack=[%[x],%[x]) heap=[%[x],%[x]) ring=%u",
	// 	tb->context.IP, tb->context.SP, tb->stack_lineaddr, stack_loc_top, pb->heapbtm, pb->heaptop, ring);
	Taskman::SetSegment(&tb->context);
	#if (_MCCA & 0xFF00) == 0x8600
	// Initialize FPU/SSE context to a safe state (masked exceptions)
	// Offset 0: FPU Control Word (0x037F = Mask all exceptions)
	treat<uint16>(&tb->context.floating_point_context[0]) = 0x037F;
	// Offset 24: MXCSR (0x1F80 = Mask all SSE exceptions)
	treat<uint32>(&tb->context.floating_point_context[24]) = 0x1F80;
	#endif

	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	constexpr stduint floating_support = (1 << 13);
	tb->context.sp = initial_sp;
	tb->context.IP = _IMM(header.e_entry) + load_bias;
	tb->context.mstatus = (ring << 11) | floating_support | _MSTATUS_MPIE;
	tb->context.kernel_sp = _IMM(tb->stack_levladdr) + tb->stack_size - 0x10;

	#endif

	if (interp_d) {
		stduint interp_entry = 0;
		if (!_Taskman_Load_Interp(interp_d, pb, (byte*)block_buffer.reflect(), load_slice_p, &interp_entry)) {
			return nullptr;
		}
		tb->context.IP = interp_entry; // Override entry point
	}

	tb->priority = (ring != RING_M) ? 4 : 0;
	tb->time_slice = (ring != RING_M) ? 3 : 4;
	if (append) {
		Taskman::Append(pb);
		Taskman::AppendThread(tb);
	}
	#if CONFIG_ENABLE_MMU
	pin_scope.Keep();
	#endif
	return pb;
	#elif !CONFIG_ENABLE_MMU
	// NoMMU: one contiguous flat region holds the whole image, relocations are applied in place
	String block_buffer(String::Charset::Memory, 512);
	struct ELF_Header_t header;
	source->Read(0, &header, sizeof(header), (byte*)block_buffer.reflect());
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	if (header.e_type != ET_DYN || !header.e_phnum) {
		plogerro("%s: only ET_DYN (static PIE) is loadable without MMU", __FUNCIDEN__);
		return nullptr;
	}
	stduint image_span = 0, image_align = 0x1000;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		if (ph.p_vaddr + ph.p_memsz > image_span) image_span = ph.p_vaddr + ph.p_memsz;
		if (ph.p_align > image_align) image_align = ph.p_align;
	}
	if (!image_span) {
		plogerro("%s: no PT_LOAD segment", __FUNCIDEN__);
		return nullptr;
	}
	stduint align_expo = 12;// 4KB meets the usual p_align, mempool alignment is a power-of-two exponent
	while ((1u << align_expo) < image_align && align_expo < 20) align_expo++;
	constexpr stduint flat_heap_reserve = 7u << 20;// the block owns the process heap too, so its MPU region covers malloc
	while ((1u << align_expo) < image_span + flat_heap_reserve && align_expo < 24) align_expo++;// the block is its own MPU region, so it owns a whole power of two
	const stduint image_block = 1u << align_expo;
	byte* image = (byte*)mempool.allocate(image_block, align_expo);
	if (!image) {
		plogerro("%s: no memory for the image (%u bytes)", __FUNCIDEN__, image_block);
		return nullptr;
	}
	MemSet(image, 0, image_block);// zeroing keeps the BSS tail of every segment
	const stduint load_bias = _IMM(image);// a PIE links from vaddr 0, so the image base is vaddr 0
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type != PT_LOAD || !ph.p_filesz) continue;
		byte* dest = image + ph.p_vaddr;
		stduint done = 0;
		while (done < ph.p_filesz) {
			stduint chunk = ph.p_filesz - done;
			if (chunk > 0x1000) chunk = 0x1000;
			stduint got = source->Read(ph.p_offset + done, dest + done, chunk, (byte*)block_buffer.reflect());
			if (got != chunk) {
				plogerro("%s: segment read failed (%u of %u)", __FUNCIDEN__, got, chunk);
				return nullptr;
			}
			done += chunk;
		}
	}
	// a faulted task resumes here to raise a clean exit syscall
	*(uint16*)(image + image_block - 8) = 0xDF00;// svc #0
	*(uint16*)(image + image_block - 6) = 0xE7FE;// b .
	if (!_Taskman_Relocate_PIE_Flat(source, header, load_bias, (byte*)block_buffer.reflect())) {
		plogerro("%s: PIE relocation failed", __FUNCIDEN__);
		return nullptr;
	}
	#if defined(_ARCH_ARM_ProfileM)
	L1C.CleanDCacheRange(image, image_block);// the image was written through the D-cache
	L1C.InvalidateICacheAll();
	#endif
	ProcessBlock* pb = Taskman::Create((void*)((load_bias + header.e_entry) | 1), ring, false);
	if (!pb) return nullptr;
	const stduint arg_sp = Taskman::SetupStack(pb, pb, nullptr, nullptr,
		load_bias + header.e_entry, 0, header.e_phnum, header.e_phentsize);
	if (arg_sp) {
		auto& ctx = pb->main_thread->context;
		#if defined(_ARCH_ARM_ProfileM)
		stduint* frame = (stduint*)(arg_sp - 0x20);
		frame[0] = *(const stduint*)arg_sp;
		frame[1] = arg_sp + sizeof(stduint);
		frame[2] = arg_sp + (frame[0] + 2) * sizeof(stduint);
		frame[3] = 0;
		frame[4] = 0;
		frame[5] = 0;
		frame[6] = (load_bias + header.e_entry) | 1;
		frame[7] = 0x01000000;
		ctx.sp = arg_sp - 0x20;
		#else
		ctx.sp = arg_sp;
		#endif
	}
	pb->load_slices[0].address = load_bias;
	pb->load_slices[0].length = image_block;// the release and the MPU region cover the same range
	pb->heapbtm = (load_bias + image_span + 0xFFF) & ~_IMM(0xFFF);// the heap shares the image block, so its MPU region covers it
	pb->heaptop = pb->heapbtm;
	ploginfo("[ELF] flat base=%[x] span=%u entry=%[x]", load_bias, image_span, load_bias + header.e_entry);
	if (append) {
		Taskman::Append(pb);
		Taskman::AppendThread(pb->main_thread);
	}
	return pb;
	#endif
	return nullptr;
}

ProcessBlock* Taskman::CreateELF(BlockTrait* source, byte ring, bool append) {
	return CreateELFImage(source, nullptr, ring, append);
}

#if CONFIG_ENABLE_MMU
ProcessBlock* Taskman_CreateELF_File(BlockTrait* source,
	uni::vfs_file* image_file, byte ring, bool append) {
	return CreateELFImage(source, image_file, ring, append);
}

bool Taskman_ValidateELF_FileSegments(BlockTrait* source,
	const ELF_Header_t& header, stduint image_size,
	stduint load_bias, byte* buffer) {
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4))
		return false;
	auto headers = uni::make_unique<ELF_PHT_t[]>(header.e_phnum);
	return headers && PlanMainFileLoads(source, header, image_size,
		load_bias, buffer, headers.get());
}

// Map PT_LOAD pages and retain the executable inode for the image lifetime.
bool Taskman_MapELF_FileSegments(ProcessBlock* pb, BlockTrait* source,
	uni::vfs_file* image_file, const ELF_Header_t& header,
	uint64 image_version, stduint image_size, stduint load_bias,
	byte* buffer,
	stduint& load_slice_p,
	stduint& max_seg_end) {
	if (!pb || !source || !image_file || !image_file->f_inode ||
		(image_file->f_inode->i_mode & I_TYPE_MASK) != I_REGULAR)
		return false;
	auto headers = uni::make_unique<ELF_PHT_t[]>(header.e_phnum);
	if (!headers || !PlanMainFileLoads(source, header,
		image_size, load_bias, buffer, headers.get()) ||
		!uni::PinExecutableImage(pb, image_file,
			image_version)) return false;
	if (MapMainFileLoads(pb, source, image_file, headers.get(),
		header.e_phnum, load_bias, buffer, load_slice_p, max_seg_end))
		return true;
	uni::UnpinExecutableImage(pb);
	return false;
}
#endif


