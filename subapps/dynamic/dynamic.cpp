#include <stdio.h>
#include <c/format/ELF.h>

#include <string.h>
#include <stdlib.h>

#define printf(...)

int main(int argc, char* argv[], char* envp[]) {
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
				else sprintf(target_path, "/mnt/ide2.0/lib/%s", base_name);
				
				FILE* fp = fopen(target_path, "rb");
				if (!fp) {
					// Fallback to /lib/
					sprintf(target_path, "/mnt/ahci1.0/lib/%s", base_name);
					fp = fopen(target_path, "rb");
				}
				if (!fp) {
					// Fallback to /lib/
					sprintf(target_path, "/lib/%s", base_name);
					fp = fopen(target_path, "rb");
				}
				
				if (fp) {
					printf("[LD-MCCA] Successfully opened library at %s!\n", target_path);
					
					ELF_Header_t lib_header;
					fread(&lib_header, 1, sizeof(ELF_Header_t), fp);
					if (lib_header.MagicNumber[1] == 'E' && lib_header.MagicNumber[2] == 'L' && lib_header.MagicNumber[3] == 'F') {
						printf("[LD-MCCA] Verified ELF signature for %s!\n", base_name);
						
						// The library exports far more symbols than any fixed bound can guess, so read the real count from .dynsym
						unsigned int libc_sym_count = 1;
						ELF_SHT_t* sh_table = nullptr;
						if (lib_header.e_shnum && lib_header.e_shentsize >= sizeof(ELF_SHT_t)) {
							sh_table = new ELF_SHT_t[lib_header.e_shnum];
							fseek(fp, lib_header.e_shoff, SEEK_SET);
							fread(sh_table, lib_header.e_shentsize, lib_header.e_shnum, fp);
							for (int k = 0; k < lib_header.e_shnum; k++) {
								if (sh_table[k].sh_type == SHT_DYNSYM && sh_table[k].sh_entsize) {
									libc_sym_count = sh_table[k].sh_size / sh_table[k].sh_entsize;
									break;
								}
							}
						}
						printf("[LD-MCCA] Library exports %u dynamic symbols\n", libc_sym_count);
						
						// Page-align the image start per segment p_align, then keep a bias so p_vaddr maps to bias + p_vaddr
						unsigned int max_align = 0x1000;
						unsigned int min_page = 0xFFFFFFFF;
						unsigned int max_page = 0;
						
						ELF_PHT_t* ph_table = new ELF_PHT_t[lib_header.e_phnum];
						if (!ph_table) {
							plogerro("[LD-MCCA] no memory for the program headers");
							delete[] sh_table;
							break;
						}
						fseek(fp, lib_header.e_phoff, SEEK_SET);
						fread(ph_table, lib_header.e_phentsize, lib_header.e_phnum, fp);
						
						for (int k = 0; k < lib_header.e_phnum; k++) {
							if (ph_table[k].p_type == PT_LOAD) {
								unsigned int al = ph_table[k].p_align ? ph_table[k].p_align : 0x1000;
								if (al > max_align) max_align = al;
								unsigned int seg_start = ph_table[k].p_vaddr & ~(al - 1);
								if (seg_start < min_page) min_page = seg_start;
								unsigned int seg_end = (ph_table[k].p_vaddr + ph_table[k].p_memsz + al - 1) & ~(al - 1);
								if (seg_end > max_page) max_page = seg_end;
							}
						}
						if (min_page == 0xFFFFFFFF) min_page = 0;
						
						unsigned int total_size = max_page - min_page;
						printf("[LD-MCCA] Library requires %u bytes of memory space.\n", total_size);
						
						// Allocate memory for the library
						char* lib_raw = new char[total_size + max_align];
						if (!lib_raw) {
							plogerro("[LD-MCCA] no memory for %s (%u bytes)", base_name, total_size);
							delete[] ph_table;
							delete[] sh_table;
							break;
						}
						char* lib_mem = (char*)(((unsigned int)lib_raw + (max_align - 1)) & ~(max_align - 1));
						memset(lib_mem, 0, total_size);
						char* lib_base = lib_mem - min_page;
						printf("[LD-MCCA] Allocated memory for %s at 0x%08x (span %u, bias 0x%08x, align %u)\n", base_name, (unsigned int)lib_mem, total_size, (unsigned int)lib_base, max_align);
						
						// Load the segments: the page head and the bss tail stay zero from the memset
						for (int k = 0; k < lib_header.e_phnum; k++) {
							if (ph_table[k].p_type == PT_LOAD) {
								fseek(fp, ph_table[k].p_offset, SEEK_SET);
								char* dest = lib_base + ph_table[k].p_vaddr;
								fread(dest, 1, ph_table[k].p_filesz, fp);
								printf("[LD-MCCA] Loaded segment to 0x%08x (size: %d)\n", (unsigned int)dest, ph_table[k].p_filesz);
							}
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
						
						// A helper lambda or just run the loop twice
						auto process_relocs = [&](unsigned int rel_addr, unsigned int rel_sz) {
							if (!rel_addr || !rel_sz) return;
							int rel_count = rel_sz / sizeof(Elf32_Rel);
							Elf32_Rel* rel_table = (Elf32_Rel*)rel_addr;
							int processed = 0;
							Elf32_Sym* libc_syms = (Elf32_Sym*)libc_symtab;
							
							for (int i = 0; i < rel_count; i++) {
								int rtype = ELF32_R_TYPE(rel_table[i].r_info);
								unsigned int* ptr = (unsigned int*)((unsigned int)lib_base + rel_table[i].r_offset);
								
								if (rtype == R_386_RELATIVE) { // 8
									*ptr += (unsigned int)lib_base;
									processed++;
								} 
								else if (rtype == R_386_GLOB_DAT || rtype == R_386_JUMP_SLOT || rtype == R_386_32) {
									int sym_idx = ELF32_R_SYM(rel_table[i].r_info);
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
								}
							}
							printf("[LD-MCCA] Processed %d internal relocations for libc.so\n", processed);
						};
						
						// Perform internal relocations for libc.so itself
						process_relocs(libc_rel, libc_relsz);
						process_relocs(libc_jmprel, libc_pltrelsz);
						
						// Resolve one imported name against libc's dynamic symbol table
						Elf32_Sym* libc_syms = (Elf32_Sym*)libc_symtab;
						auto resolve_name = [&](const char* sym_name) -> unsigned int {
							if (!sym_name || !libc_syms || !libc_strtab) return 0;
							for (unsigned int s = 1; s < libc_sym_count; s++) {
								if (libc_syms[s].st_name == 0) continue; // Skip unnamed
								const char* l_name = (const char*)(libc_strtab + libc_syms[s].st_name);
								if (strcmp(sym_name, l_name) == 0) {
									return (unsigned int)lib_base + libc_syms[s].st_value;
								}
							}
							return 0;
						};
						
						// .rel.dyn and .rel.plt of the main executable both land here
						auto apply_main_relocs = [&](unsigned int rel_addr, unsigned int rel_sz, const char* what) {
							if (!rel_addr || !rel_sz) return;
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
						};
						
						apply_main_relocs(dt_rel, dt_relsz, "REL");
						apply_main_relocs(dt_jmprel, dt_pltrelsz, "PLT");
						
						delete[] ph_table;
						delete[] sh_table;
					} else {
						printf("[LD-MCCA] Warning: Invalid ELF signature in %s\n", target_path);
					}
					fclose(fp);
				} else {
					plogerro("[LD-MCCA] failed to open library %s", base_name);
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
