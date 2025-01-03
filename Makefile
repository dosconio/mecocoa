

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
