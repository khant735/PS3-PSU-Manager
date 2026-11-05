.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error Set PSL1GHT)
endif
include $(PSL1GHT)/ppu_rules
TARGET := tiny3d_control
BUILD := build
SOURCES := .
INCLUDES := .
TITLE := Tiny3D Standalone Control
APPID := T3DC00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
CFLAGS := -O1 -ffast-math -fno-math-errno -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
LIBS := -ltiny3d -lfont -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
LIBDIRS := -L$(PSL1GHT)/portlibs/ppu/lib
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(CURDIR)
export DEPSDIR := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)
export LD := $(CXX)
export OFILES := main.o
export INCLUDE := -I$(CURDIR) $(LIBPSL1GHT_INC) -I$(PSL1GHT)/portlibs/ppu/include
export LIBPATHS := $(LIBPSL1GHT_LIB) -L$(PSL1GHT)/portlibs/ppu/lib
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).self
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
