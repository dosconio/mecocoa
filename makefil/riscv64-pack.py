# ASCII Python TAB4 LF
# LastCheck: 20240524
# AllAuthor: @dosconio
# ModuTitle: Mecocoa riscv-64 Linkage Maker
# Copyright: Dosconio Mecocoa, BSD 3-Clause License

import os

TARGET_DIR = "/home/ayano/_obj/mcca/riscv64/app/"
OUTPUT_IDN = "riscv64/link_app.S"

if __name__ == '__main__':
    f = open(OUTPUT_IDN, mode="w")
    apps = os.listdir(TARGET_DIR)
    apps.sort()
    f.write(
'''    .align 4
    .section .data
    .global _app_num
_app_num:
    .quad {}
'''.format(len(apps))
    )

    for (idx, _) in enumerate(apps):
        f.write('    .quad app_{}_start\n'.format(idx))
    f.write('    .quad app_{}_end\n'.format(len(apps) - 1))

    f.write(
'''
    .global _app_names
_app_names:
''');

    for app in apps:
        f.write("   .string \"" + app + "\"\n")

    for (idx, app) in enumerate(apps):
        f.write(
'''
    .section .data.app{0}
    .global app_{0}_start
app_{0}_start:
    .incbin "{1}"
'''.format(idx, TARGET_DIR + app)
        )
    f.write('app_{}_end:\n\n'.format(len(apps) - 1))
    f.close()
