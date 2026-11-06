PS3DEV ?= /usr/local/ps3dev
PSL1GHT ?= $(PS3DEV)
# Container sets PSL1GHT to PS3DEV root; use actual SDK root for headers.
SDK_INC := $(if $(wildcard $(PSL1GHT)/ppu/include/sysutil/video.h),$(PSL1GHT)/ppu/include,$(PS3DEV)/ppu/include)
CC := $(PS3DEV)/ppu/bin/powerpc64-ps3-elf-g++
STRIP := $(PS3DEV)/ppu/bin/powerpc64-ps3-elf-strip
TOOL := $(PS3DEV)/bin
CONTENTID := UP0001-T3DC00001_00-0000000000000000
CXXFLAGS := -O1 -ffast-math -fno-math-errno -mcpu=cell -fno-exceptions -fno-rtti -I$(SDK_INC) -I$(PS3DEV)/portlibs/ppu/include
LIBS := -L$(PS3DEV)/portlibs/ppu/lib -L$(PSL1GHT)/ppu/lib -ltiny3d -lfont -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

.PHONY: all clean
all: tiny3d_control.elf tiny3d_control.self Tiny3D-Control.pkg
main.o: main.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@
tiny3d_control.elf: main.o
	$(CC) -mcpu=cell -Wl,-Map,tiny3d_control.map -o $@ $< $(LIBS)
tiny3d_control.self: tiny3d_control.elf
	mkdir -p stage
	$(STRIP) $< -o stage/tiny3d_control.elf
	$(TOOL)/sprxlinker stage/tiny3d_control.elf
	$(TOOL)/make_self stage/tiny3d_control.elf $@
Tiny3D-Control.pkg: tiny3d_control.self
	mkdir -p stage/pkg/USRDIR
	$(TOOL)/make_self_npdrm stage/tiny3d_control.elf stage/pkg/USRDIR/EBOOT.BIN $(CONTENTID)
	cp $(TOOL)/ICON0.PNG stage/pkg/ICON0.PNG
	$(TOOL)/sfo --title "Tiny3D Standalone Control" --appid T3DC00001 -f $(TOOL)/sfo.xml stage/pkg/PARAM.SFO
	$(TOOL)/pkg --contentid $(CONTENTID) stage/pkg/ $@
	cp $@ Tiny3D-Control.gnpdrm.pkg
	$(TOOL)/package_finalize Tiny3D-Control.gnpdrm.pkg
clean:
	rm -rf main.o stage tiny3d_control.map tiny3d_control.elf tiny3d_control.self Tiny3D-Control.pkg Tiny3D-Control.gnpdrm.pkg
