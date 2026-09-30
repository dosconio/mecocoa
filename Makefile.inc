
# AASM GNUAS C CPP
asmpref=_ae_
gaspref=_ag_
cplpref=_cc_
cpppref=_cx_
dest_obj=$(uobjpath)/mcca-$(arch)

define asm_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.asm=.o)): $(1)
endef
define gas_to_o
$(dest_obj)/$(gaspref)$(notdir $(1:.S=.o)): $(1)
endef
define c_to_o
$(dest_obj)/$(cplpref)$(notdir $(1:.c=.o)): $(1)
endef
define cpp_to_o
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.o)): $(1)
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
	$(ulibpath)/cpp/system/paging.cpp \
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
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(gasfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))
