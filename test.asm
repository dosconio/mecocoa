[CPU 386]
DB 2

CLI

MOV SI, msg
MOV AH, 0
INT 80H

HLT
JMP $

msg: DB "Ciallo, Mecocoa~"
TIMES 1024-($-$$) DB 0
