// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#if !CONFIG_ENABLE_MMU
#define MemCopyP(a,b,c,d,e) _IMM((byte*)MemCopyN(a,c,e)-(byte*)a)
#define StrCopyP(a,b,c,d,e) _IMM((byte*)StrCopyN(a,c,e)-(byte*)a)
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
	stduint bytes_read = 0; 
	for0(i, (mem_length + compensation + 0xFFF) / 0x1000) {
		auto page_entry = pg.getEntry(_IMM(v_start1));
		stduint phy;
		
		if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
			phy = _IMM(mempool.allocate(0x1000, 12));
			MemSet((void*)phy, 0, 0x1000);
			stduint pgprop = PGPROP_present;
			if (writable) pgprop |= PGPROP_writable;
			if (user) pgprop |= PGPROP_user_access;
			if (!executable) pgprop |= PGPROP_nonexecutable;
			pg.Map(v_start1, phy, 0x1000, PAGESIZE_4KB, pgprop);
			page_entry = pg.getEntry(_IMM(v_start1));
			if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
				plogerro("Mapping failed %[x] -> %[x]", v_start1, phy);
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

bool _Taskman_Relocate_PIE(BlockTrait* source, const ELF_Header_t& header, stduint load_bias, Paging& pg, byte* block_buffer) {
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
						void* phy_page = pg[target_vaddr & ~_IMM(0xFFF)];
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
						void* phy_page = pg[target_vaddr & ~_IMM(0xFFF)];
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
		for0(r, rel_count) {
			Elf32_Rel rel;
			source->Read(rel_file_off + r * sizeof(Elf32_Rel), &rel, sizeof(rel), block_buffer);
			const stduint rel_type = ELF32_R_TYPE(rel.r_info);
			if (!rel_type) continue;
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
		const auto& vroot = Filesys::GetSystemVirtualRootPath();
		if (vroot.getByteCount() > 0) {
			real_interp_path = String::newFormat("%s%s", vroot.reference(), interp_buf.reference());
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

ProcessBlock* Taskman::CreateELF(BlockTrait* source, byte ring, bool append) {
	#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
	String block_buffer(String::Charset::Memory, 512);
	struct ELF_Header_t header;
	source->Read(0, &header, sizeof(header), (byte*)block_buffer.reflect());
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	
	ProcessBlock* pb = Taskman::AllocateTask();
	pb->ring = ring;
	pb->parent_id = Task_Kernel;
	*pb->focus_tty.Lock() = nullptr;
	
	auto tb = AllocateThread();
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
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		stduint ph_offset = header.e_phoff + header.e_phentsize * i;
		source->Read(ph_offset, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type == PT_DYNAMIC) {
			// PIE STATIC will have PT_DYNAMIC, do not abort
		}
		if (ph.p_type == PT_PHDR) phdr_addr = ph.p_vaddr + load_bias;
		if (ph.p_type == PT_LOAD && ph.p_offset == 0 && !phdr_addr) phdr_addr = ph.p_vaddr + load_bias + header.e_phoff;
		if (ph.p_type == PT_LOAD && ph.p_memsz) 
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
	if (!_Taskman_Relocate_PIE(source, header, load_bias, pb->paging, (byte*)block_buffer.reflect())) {
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
	SetSegment(&tb->context);
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
	while ((1u << align_expo) < image_span && align_expo < 20) align_expo++;// the block is its own MPU region, so it owns a whole power of two
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
	ProcessBlock* pb = Taskman::Create((void*)((load_bias + header.e_entry) | 1), ring, append);
	if (!pb) return nullptr;
	pb->load_slices[0].address = load_bias;
	pb->load_slices[0].length = image_block;// the release and the MPU region cover the same range
	ploginfo("[ELF] flat base=%[x] span=%u entry=%[x]", load_bias, image_span, load_bias + header.e_entry);
	return pb;
	#endif
	return nullptr;
}


