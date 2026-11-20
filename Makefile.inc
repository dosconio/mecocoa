
# AASM GNUAS C CPP
asmpref=_ae_
gaspref=_ag_
cplpref=_c_
cpppref=_x_
unicplpref=_cc_
unicpppref=_cx_
# Keep source-to-object mapping identical for prerequisites and linker inputs.
c_object=$(dest_obj)/$(if $(filter $(abspath $(ulibpath))/%,$(abspath $(1))),$(unicplpref),$(cplpref))$(notdir $(1:.c=.o))
cpp_object=$(dest_obj)/$(if $(filter $(abspath $(ulibpath))/%,$(abspath $(1))),$(unicpppref),$(cpppref))$(notdir $(1:.cpp=.o))
dest_obj=$(uobjpath)/mcca-$(arch)

define asm_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.asm=.o)): $(1)
endef
define gas_to_o
$(dest_obj)/$(gaspref)$(notdir $(1:.S=.o)): $(1)
endef
define c_to_o
$(call c_object,$(1)): $(1) Makefile.inc
endef
define cpp_to_o
$(call cpp_object,$(1)): $(1) Makefile.inc
endef


cppfile+=$(wildcard mecocoa/*.cpp) prehost/_auxiliary.cpp $(wildcard devdriv/*.cpp) $(wildcard devdriv/**/*.cpp) \
	$(ulibpath)/cpp/sort.cpp \
	$(ulibpath)/cpp/consio.cpp \
	$(ulibpath)/cpp/stream.cpp \
	$(ulibpath)/cpp/string.cpp \
	$(ulibpath)/cpp/interrupt.cpp \
	$(ulibpath)/cpp/Device/PLIC.cpp \
	$(ulibpath)/cpp/Device/UART.cpp \
	$(ulibpath)/cpp/Device/Timer.cpp \
	$(ulibpath)/cpp/Datype/Map/Paging.cpp \
	$(ulibpath)/cpp/System/Filesys.cpp \
	$(ulibpath)/cpp/Format/Filesys/CD.cpp \
	$(ulibpath)/cpp/Lango/lango-cpp.cpp \
	$(ulibpath)/cpp/Datype/bstring.cpp \
	$(ulibpath)/cpp/Datype/mempool.cpp \
	$(ulibpath)/cpp/Datype/bmmemoman.cpp \
	$(wildcard $(ulibpath)/cpp/Auxiliary/*.cpp) \
	$(ulibpath)/cpp/Datype/dnode.cpp $(wildcard $(ulibpath)/cpp/Datype/dnode/*.cpp) \
	$(ulibpath)/cpp/Datype/nnode.cpp $(wildcard $(ulibpath)/cpp/Datype/nnode/*.cpp) \
	$(ulibpath)/cpp/Device/Storage.cpp $(ulibpath)/cpp/Device/Storage/Storage-GPT.cpp \
	$(ulibpath)/cpp/Format/Filesys/FAT.cpp $(wildcard $(ulibpath)/cpp/Format/Filesys/FAT/*.cpp) \

kernel_excluded_cpp+=devdriv/video/video-bochs.cpp
kernel_excluded_cpp+=devdriv/net/e1000.cpp
kernel_excluded_cpp+=devdriv/net/lance.cpp
kernel_excluded_cpp+=devdriv/vmhost/vmci.cpp
cppfile:=$(filter-out $(kernel_excluded_cpp),$(cppfile))

cplfile+= \
	$(ulibpath)/c/mcore.c \
	$(ulibpath)/c/debug.c \
	$(wildcard $(ulibpath)/c/dnode/*.c) \
	$(wildcard $(ulibpath)/c/nnode/*.c) \
	$(ulibpath)/c/ustring/astring/salc.c \
	$(ulibpath)/c/ustring/astring/StrHeap.c $(ulibpath)/c/ustring/astring/StrHeapN.c \

asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %asm,%o,$(notdir $(asmfile))))
gasobjs=$(addprefix $(dest_obj)/$(gaspref),$(patsubst %S,%o,$(notdir $(gasfile))))
cppobjs=$(sort $(foreach src,$(cppfile),$(call cpp_object,$(src))))
cplobjs=$(sort $(foreach src,$(cplfile),$(call c_object,$(src))))


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(gasfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))
