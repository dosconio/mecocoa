

# e.g., use `make run arch=a` to assign arch
arch?=atx-x86-flap32

build:
	@make -f configs/$(arch).make build --silent

run:
	@make -f configs/$(arch).make run --silent

clean:

read:
	readelf -a $(ubinpath)/$(arch).elf

lib:
	cd $(ulibpath)/.. && make mx86 -j

###
FLAG_RV32=qemuvirt-r32

build-r32:
	@arch=$(FLAG_RV32) make -f configs/$(FLAG_RV32).make build --silent
run-r32:
	@arch=$(FLAG_RV32) make -f configs/$(FLAG_RV32).make run --silent
lib-r32:
	# _TODO cd $(ulibpath)/.. && make mr32 -j
