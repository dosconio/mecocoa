# ASCII Makefile TAB4 LF
# Attribute: Shell(Bash)
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

# Use `make run arch=a` to assign arch, or export arch for global environmental variable
### atx-x86-flap32
### atx-x64-uefi64
### qemuvirt-r32
arch?=atx-x86-flap32

.PHONY: build config run debug clean clean-app read run-only line devenv-arch acpi-test-table

IASL ?= iasl
PYTHON ?= python3
acpi-test ?= $(ACPI_TEST)
acpi_test_source=configs/acpi-test/qemu-positive.asl
acpi_test_generator=configs/acpi-test/generate-qemu-positive.py
acpi_test_table=$(uobjpath)/acpi-test/qemu-positive.aml
acpi_test_enabled=$(filter 1 y yes true on,$(acpi-test))
acpi_test_prerequisite=$(if $(acpi_test_enabled),$(acpi_test_table))
acpi_test_qemu_arg=$(if $(acpi_test_enabled),-acpitable file=$(acpi_test_table))
acpi-dump ?= $(ACPI_DUMP)
acpi_dump_converter=configs/acpi-test/prepare-firmware-dsdt.py
acpi_dump_dsdt_source=$(acpi-dump)/static/DSDT
acpi_dump_dsdt_table=$(uobjpath)/acpi-test/firmware-dsdt-as-ssdt.aml
acpi_dump_ssdt_tables=$(sort $(wildcard $(acpi-dump)/static/SSDT*))
acpi_dump_enabled=$(strip $(acpi-dump))
acpi_dump_prerequisite=$(if $(acpi_dump_enabled),$(acpi_dump_dsdt_table))
acpi_dump_qemu_arg=$(if $(acpi_dump_enabled),-acpitable file=$(acpi_dump_dsdt_table) $(foreach table,$(acpi_dump_ssdt_tables),-acpitable file=$(table)))

build:
	@make -f configs/$(arch).make build --silent

config:
	@menuconfig
	@genconfig --header-path include/autoconf.h

run: $(acpi_test_prerequisite) $(acpi_dump_prerequisite)
	@make -f configs/$(arch).make run qemu_extra_args="$(qemu_extra_args) $(acpi_test_qemu_arg) $(acpi_dump_qemu_arg)" --silent

debug:
	@make -f configs/$(arch).make debug

clean:
	cd $(ulibpath)/.. && make clean
	@make -f configs/$(arch).make clean --silent

clean-app:
	@make -f configs/$(arch).make clean-app --silent

read:
	readelf -a $(ubinpath)/$(arch).elf

run-only: $(acpi_test_prerequisite) $(acpi_dump_prerequisite)
	@make -f configs/$(arch).make run-only qemu_extra_args="$(qemu_extra_args) $(acpi_test_qemu_arg) $(acpi_dump_qemu_arg)"

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
	# make arch=atx-x64-long64
	#
	make arch=raspi3-ac53
	make arch=qemuvirt-a64
	#
	make arch=qemuvirt-r32
	make arch=qemuvirt-r64

###

$(acpi_test_table): $(acpi_test_source) $(acpi_test_generator)
	@mkdir -p $(dir $@)
	@if command -v "$(IASL)" >/dev/null 2>&1; then \
		$(IASL) -p $(basename $@) $(acpi_test_source); \
	else \
		echo "[ACPI TEST] iasl unavailable; using fixed AML generator"; \
		$(PYTHON) $(acpi_test_generator) $@; \
	fi

$(acpi_dump_dsdt_table): $(acpi_dump_dsdt_source) $(acpi_dump_converter)
	@mkdir -p $(dir $@)
	@$(PYTHON) $(acpi_dump_converter) $(acpi_dump_dsdt_source) $@

acpi-test-table: $(acpi_test_table)
	@echo "ACPI test table: $(acpi_test_table)"

