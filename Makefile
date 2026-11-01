.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment")
endif
include $(PSL1GHT)/ppu_rules

TARGET := ps3psumanager
BUILD := build
SOURCES := source vendor/lzma/C
INCLUDES := include vendor/lzma/C
TITLE := PS2 .psu Save Manager v1.64
APPID := PSU000001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
ICON0 := $(CURDIR)/assets/ICON0.PNG
SFOXML := $(CURDIR)/assets/sfo.xml
PKGFILES := $(CURDIR)/pkgfiles
CFLAGS := -DGFS_ENABLE_7Z -O1 -ffast-math -fno-math-errno -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS := $(MACHDEP) -Wl,-Map,$(notdir $@).map -Wl,--wrap=rsxtiny_Init -Wl,--wrap=gcmGetFlipStatus -Wl,--wrap=gcmResetFlipStatus -Wl,--wrap=gcmSetFlip -Wl,--wrap=gcmSetWaitFlip -Wl,--wrap=gcmSetFlipMode -Wl,--wrap=rsxtiny_MakeCmdSpace -Wl,--wrap=tiny_rsxContextCallback -Wl,--wrap=setupRenderTarget -Wl,--wrap=sysUtilCheckCallback
LIBS := -lfont3d -ltiny3d -lfont -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnet -lnetctl -lsysmodule -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
LIBDIRS := -L$(PSL1GHT)/portlibs/ppu/lib

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)
LZMA_CFILES := 7zAlloc.c 7zArcIn.c 7zBuf.c 7zBuf2.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zFile.c 7zStream.c Bcj2.c Bra.c Bra86.c BraIA64.c CpuArch.c Delta.c Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c
CFILES := $(notdir $(wildcard source/*.c)) $(LZMA_CFILES)
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export LD := $(CXX)
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(LIBPSL1GHT_INC) -I$(PSL1GHT)/portlibs/ppu/include -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(LIBPSL1GHT_LIB) -L$(PSL1GHT)/portlibs/ppu/lib
.PHONY: all clean pkg run host-test $(BUILD)
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).pkg
pkg: $(BUILD)
	@mkdir -p $(PKGFILES)/USRDIR
	@cp PS2_GAME_TITLES.tsv $(PKGFILES)/USRDIR/PS2_GAME_TITLES.tsv
	@$(MAKE) --no-print-directory -W $(OUTPUT).self $(OUTPUT).pkg
	@test -f $(BUILDDIR)/pkg/PARAM.HIP || (echo "ERROR: PARAM.HIP missing from package staging"; exit 1)
	@grep -q "PS2 .PSU SAVE MANAGER v1.64" $(BUILDDIR)/pkg/PARAM.HIP || (echo "ERROR: PARAM.HIP version text mismatch"; exit 1)
	@test -s $(BUILDDIR)/pkg/USRDIR/cacert.pem || (echo "ERROR: CA bundle missing from package staging"; exit 1)
	@cmp -s PS2_GAME_TITLES.tsv $(BUILDDIR)/pkg/USRDIR/PS2_GAME_TITLES.tsv || (echo "ERROR: bundled PS2 title database missing or mismatched"; exit 1)
	@test -s $(PKGFILES)/USRDIR/PS2_GAME_TITLES.releases.tsv || (echo "ERROR: PS2 release-date database missing from package inputs"; exit 1)
	@cmp -s $(PKGFILES)/USRDIR/PS2_GAME_TITLES.releases.tsv $(BUILDDIR)/pkg/USRDIR/PS2_GAME_TITLES.releases.tsv || (echo "ERROR: PS2 release-date database missing or mismatched in package"; exit 1)
run: $(BUILD)
	ps3load $(OUTPUT).self
host-test:
	bash tests/run_host_tests.sh
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
