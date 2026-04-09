# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash)
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

# Use `make run arch=a` to assign arch, or export arch for global environmental variable
### atx-x86-flap32
### atx-x64-uefi64
### qemuvirt-r32
arch?=atx-x86-flap32

build:
	@make -f configs/$(arch).make build --silent

run:
	@make -f configs/$(arch).make run --silent

debug:
	@make -f configs/$(arch).make debug

clean:
	cd $(ulibpath)/.. && make clean
	@make -f configs/$(arch).make clean --silent

pack:
	@make -f configs/$(arch).make pack --silent

read:
	readelf -a $(ubinpath)/$(arch).elf

run-only:
	@make -f configs/$(arch).make run-only

###
FLAG_RV32=qemuvirt-r32

build-r32:
	@arch=$(FLAG_RV32) make -f configs/$(FLAG_RV32).make build --silent
run-r32:
	@arch=$(FLAG_RV32) make -f configs/$(FLAG_RV32).make run --silent
lib-r32:
	# _TODO cd $(ulibpath)/.. && make mr32 -j


###

.PHONY : lib-all
lib-all:
	make -f accmlib/accmrv32.make
	make -f accmlib/accmrv64.make
	make -f accmlib/accmx86.make

.PHONY : all
all: lib-all
	#
	make arch=atx-x86-flap32
	make arch=atx-x64-uefi64
	make arch=atx-x64-long64
	#
	make arch=raspi3-ac53
	make arch=qemuvirt-a64
	#
	make arch=qemuvirt-r32
	make arch=qemuvirt-r64
