.PHONY: x86 x64 ac7m ac7m-gcc clean clean-x86 clean-x64 clean-ac7m clean-ac7m-gcc

x86:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=x86

x64:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=x64

ac7m:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=ac7m

ac7m-gcc:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=ac7m-gcc

clean: clean-x86 clean-x64 clean-ac7m clean-ac7m-gcc

clean-x86:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=x86 clean

clean-x64:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=x64 clean

clean-ac7m:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=ac7m clean

clean-ac7m-gcc:
	$(MAKE) -C x86 -f Makefile.mcca target_arch=ac7m-gcc clean
