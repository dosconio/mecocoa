# ASCII Python TAB4 LF
# LastCheck: 20240523
# AllAuthor: @dosconio
# ModuTitle: Mecocoa riscv-64 Linkage Maker
# Copyright: Dosconio Mecocoa, BSD 3-Clause License

# call this from the path of root project

import os

TARGET_DIR = "subapps/riscv64/"
LINKER_OUT = "../_obj/mcca/riscv64/kernel.ld"

if __name__ == '__main__':
    f = open(LINKER_OUT, mode="w")
    apps = os.listdir(TARGET_DIR)
    f.write(
'''/* ASCII LINK-SCRIPT TAB4 LF
*/
OUTPUT_ARCH(riscv)
ENTRY(_entry)
BASE_ADDRESS = 0x80200000;

SECTIONS
{
    . = BASE_ADDRESS;
    skernel = .;

    s_text = .;
    .text : {
        *(.text.entry)
        *(.text .text.*)
        . = ALIGN(0x1000);
        *(trampsec)
        . = ALIGN(0x1000);
    }

    . = ALIGN(4K);
    e_text = .;
    s_rodata = .;
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4K);
    e_rodata = .;
    s_data = .;
    .data : {
        *(.data)
''')
    for (idx, _) in enumerate(apps):
        f.write('        . = ALIGN(0x1000);\n')
        f.write('        *(.data.app{})\n'.format(idx))
    f.write(
'''
        *(.data.*)
        *(.sdata .sdata.*)
    }
    
    . = ALIGN(4K);
    e_data = .;
    .bss : {
        *(.bss.stack)
        s_bss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    }

    . = ALIGN(4K);
    e_bss = .;
    ekernel = .;

    /DISCARD/ : {
        *(.eh_frame)
    }
}
''')
    f.close()
