---
dg-publish: true
her-note: false
---

# Modules

## handler

`Static `: Process signals from interrrupts and exception.

## syscall

`Static `: Process Callings from programs.

## console

`Static `: Manage TTY (BCon and VCon)
`Dynamic`: 

Flag: `work_console`

## fileman

`Static `: Manage FileSystem
`Dynamic`: 

Flag: `flag_ready_fileman`

## fileman-hd

Name: Hrddisk
`Static `: Manage Harddisk
`Dynamic`: 

Flag: `fileman_hd_ready`

## fileman-fs

`Static `: Manage FileSystem `OrangeS-FS`

## memoman

`Static `: Manage Memory
`Dynamic`: 

## taskman

`Static `: Manage Task
`Dynamic`: 
