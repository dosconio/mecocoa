accfile=$(wildcard accmlib/*.c)
accobjs=$(notdir $(patsubst %c, %o, $(accfile)))
aasfile=$(wildcard accmlib/*.asm)
aasobjs=$(notdir $(patsubst %asm, %o, $(aasfile)))


libusr: $(accobjs) $(aasobjs)

%.o: accmlib/%.asm
	@echo AS $(<)
	@aasm -felf $(<) -o $(uobjpath)/accm-$(arch)/$(@)

%.o: accmlib/%.c
	@echo CC $(<)
	@gcc -c -m32 -nostdlib -fno-pic -static -I$(uincpath) -D_ACCM=0x8632  $(<) -o $(uobjpath)/accm-$(arch)/$(@)
