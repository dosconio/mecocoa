#include <stdio.h>
#include <c/format/ELF.h>

#include <string.h>

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
    
    if (dynamic_addr) {
        Elf32_Dyn* dyn = (Elf32_Dyn*)dynamic_addr;
        while (dyn->d_tag != DT_NULL) {
            if (dyn->d_tag == DT_STRTAB) dt_strtab = dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_SYMTAB) dt_symtab = dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_JMPREL) dt_jmprel = dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_PLTRELSZ) dt_pltrelsz = dyn->d_un.d_val;
            dyn++;
        }
        
        printf("[LD-MCCA] DT_STRTAB: 0x%08x\n", dt_strtab);
        printf("[LD-MCCA] DT_SYMTAB: 0x%08x\n", dt_symtab);
        printf("[LD-MCCA] DT_JMPREL: 0x%08x (Size: %d bytes)\n", dt_jmprel, dt_pltrelsz);
        
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
                sprintf(target_path, "/mnt/ide2.0/lib/%s", base_name);
                
                FILE* fp = fopen(target_path, "rb");
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
                        
                        // Find the total memory footprint required by the library
                        unsigned int min_vaddr = 0xFFFFFFFF;
                        unsigned int max_vaddr = 0;
                        
                        ELF_PHT_t* ph_table = new ELF_PHT_t[lib_header.e_phnum];
                        fseek(fp, lib_header.e_phoff, SEEK_SET);
                        fread(ph_table, lib_header.e_phentsize, lib_header.e_phnum, fp);
                        
                        for (int k = 0; k < lib_header.e_phnum; k++) {
                            if (ph_table[k].p_type == PT_LOAD) {
                                if (ph_table[k].p_vaddr < min_vaddr) min_vaddr = ph_table[k].p_vaddr;
                                unsigned int end_vaddr = ph_table[k].p_vaddr + ph_table[k].p_memsz;
                                if (end_vaddr > max_vaddr) max_vaddr = end_vaddr;
                            }
                        }
                        
                        unsigned int total_size = max_vaddr - min_vaddr;
                        printf("[LD-MCCA] Library requires %u bytes of memory space.\n", total_size);
                        
                        // Allocate memory for the library
                        char* lib_base = new char[total_size];
                        memset(lib_base, 0, total_size);
                        printf("[LD-MCCA] Allocated memory for %s at 0x%08x\n", base_name, (unsigned int)lib_base);
                        
                        // Load the segments
                        for (int k = 0; k < lib_header.e_phnum; k++) {
                            if (ph_table[k].p_type == PT_LOAD) {
                                fseek(fp, ph_table[k].p_offset, SEEK_SET);
                                char* dest = lib_base + (ph_table[k].p_vaddr - min_vaddr);
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
                                Elf32_Dyn* libc_dyn = (Elf32_Dyn*)(lib_base + (ph_table[k].p_vaddr - min_vaddr));
                                while (libc_dyn->d_tag != DT_NULL) {
                                    if (libc_dyn->d_tag == DT_STRTAB) libc_strtab = (unsigned int)lib_base + (libc_dyn->d_un.d_ptr - min_vaddr);
                                    if (libc_dyn->d_tag == DT_SYMTAB) libc_symtab = (unsigned int)lib_base + (libc_dyn->d_un.d_ptr - min_vaddr);
                                    if (libc_dyn->d_tag == DT_REL) libc_rel = (unsigned int)lib_base + (libc_dyn->d_un.d_ptr - min_vaddr);
                                    if (libc_dyn->d_tag == DT_RELSZ) libc_relsz = libc_dyn->d_un.d_val;
                                    if (libc_dyn->d_tag == DT_JMPREL) libc_jmprel = (unsigned int)lib_base + (libc_dyn->d_un.d_ptr - min_vaddr);
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
                                unsigned int* ptr = (unsigned int*)((unsigned int)lib_base + (rel_table[i].r_offset - min_vaddr));
                                
                                if (rtype == R_386_RELATIVE) { // 8
                                    *ptr += ((unsigned int)lib_base - min_vaddr);
                                    processed++;
                                } 
                                else if (rtype == R_386_GLOB_DAT || rtype == R_386_JUMP_SLOT || rtype == R_386_32) {
                                    int sym_idx = ELF32_R_SYM(rel_table[i].r_info);
                                    unsigned int resolved_addr = 0;
                                    
                                    if (libc_syms[sym_idx].st_shndx != SHN_UNDEF) {
                                        resolved_addr = (unsigned int)lib_base + (libc_syms[sym_idx].st_value - min_vaddr);
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
                        
                        // Perform GOT/PLT Relocations for the main executable
                        if (dt_jmprel && dt_pltrelsz && libc_symtab && libc_strtab) {
                            int rel_count = dt_pltrelsz / sizeof(Elf32_Rel);
                            Elf32_Rel* rel_table = (Elf32_Rel*)dt_jmprel;
                            Elf32_Sym* main_symtab = (Elf32_Sym*)dt_symtab;
                            const char* main_strtab = (const char*)dt_strtab;
                            
                            printf("[LD-MCCA] Processing %d PLT relocations...\n", rel_count);
                            for (int i = 0; i < rel_count; i++) {
                                int sym_idx = ELF32_R_SYM(rel_table[i].r_info);
                                int rel_type = ELF32_R_TYPE(rel_table[i].r_info);
                                
                                const char* sym_name = main_strtab + main_symtab[sym_idx].st_name;
                                
                                // Simple linear search in libc's symtab
                                Elf32_Sym* libc_syms = (Elf32_Sym*)libc_symtab;
                                unsigned int resolved_addr = 0;
                                for (int s = 1; s < 2000; s++) {
                                    if (libc_syms[s].st_name == 0) continue; // Skip unnamed
                                    const char* l_name = (const char*)(libc_strtab + libc_syms[s].st_name);
                                    if (strcmp(sym_name, l_name) == 0) {
                                        resolved_addr = (unsigned int)lib_base + (libc_syms[s].st_value - min_vaddr);
                                        break;
                                    }
                                }
                                
                                if (resolved_addr) {
                                    printf("[LD-MCCA] Relocating %s to 0x%08x\n", sym_name, resolved_addr);
                                    unsigned int* got_entry = (unsigned int*)rel_table[i].r_offset;
                                    *got_entry = resolved_addr;
                                } else {
                                    printf("[LD-MCCA] Error: Symbol %s not found in libc!\n", sym_name);
                                }
                            }
                        }
                        
                        delete[] ph_table;
                    } else {
                        printf("[LD-MCCA] Warning: Invalid ELF signature in %s\n", target_path);
                    }
                    fclose(fp);
                } else {
                    printf("[LD-MCCA] Failed to open library %s anywhere!\n", base_name);
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
