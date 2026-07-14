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

config:
	@menuconfig
	@genconfig --header-path include/autoconf.h

run:
	@make -f configs/$(arch).make run --silent

debug:
	@make -f configs/$(arch).make debug

clean:
	cd $(ulibpath)/.. && make clean
	@make -f configs/$(arch).make clean --silent

clean-app:
	@make -f configs/$(arch).make clean-app --silent

pack:
	@make -f configs/$(arch).make pack --silent

read:
	readelf -a $(ubinpath)/$(arch).elf

run-only:
	@make -f configs/$(arch).make run-only

line:
	@ total=0; \
	for file in mecocoa/* include/*.hpp; do \
		if [ -f "$$file" ]; then \
			count=$$(grep -v '^[[:space:]]*$$' "$$file" | wc -l); \
			printf "%s\t: %s\n" "$$count" "$$file"; \
			total=$$((total + count)); \
		fi; \
	done; \
	printf "%s\t: TOTAL\n" "$$total"

devenv-arch:
	sudo pacman -S lib32-glibc gcc-multilib tree
	yay -S riscv-gnu-toolchain-bin
	sudo pacman -S dotnet-sdk
# 	dotnet tool install -g dotnet-script # dotnet script test.csx

###

.PHONY : lib-all
lib-all:
	make -f accmlib/accmrv32.make
	make -f accmlib/accmrv64.make
	make -f accmlib/accmx86.make
	make -f accmlib/accmx64.make

.PHONY : all
all:
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
