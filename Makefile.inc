
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
	$(ulibpath)/cpp/Device/Storage.cpp \
	$(ulibpath)/cpp/lango/lango-cpp.cpp \
	$(ulibpath)/cpp/grp-base/bstring.cpp \
	$(ulibpath)/cpp/dat-block/mempool.cpp \
	$(ulibpath)/cpp/dat-block/bmmemoman.cpp \
	$(ulibpath)/cpp/nodes/dnode.cpp $(wildcard $(ulibpath)/cpp/nodes/dnode/*.cpp) \
	$(ulibpath)/cpp/filesystem/FAT.cpp $(wildcard $(ulibpath)/cpp/filesystem/FAT/*.cpp) \

cplfile+= \
	$(ulibpath)/c/mcore.c \
	$(ulibpath)/c/debug.c \
	$(ulibpath)/c/auxiliary/toxxxer.c \
	$(wildcard $(ulibpath)/c/dnode/*.c) \
	$(ulibpath)/c/ustring/astring/salc.c \
	$(ulibpath)/c/ustring/astring/StrHeap.c \

asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %asm,%o,$(notdir $(asmfile))))
gasobjs=$(addprefix $(dest_obj)/$(gaspref),$(patsubst %S,%o,$(notdir $(gasfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(gasfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))
