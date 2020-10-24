EXAMPLES:= \
	c++ \
	cdblock \
	cpu-divu \
	cpu-dmac \
	cpu-dual \
	cpu-frt \
	cpu-wdt \
	dram-cart \
	romdisk \
	scu-dma \
	scu-dsp \
	scu-dsp-test \
	vdp1-balls \
	vdp1-double-interlace \
	vdp1-drawing \
	vdp1-mic3d \
	vdp1-zoom-sprite \
	vdp2-24bpp-bitmap \
	vdp2-2x2-plane \
	vdp2-back-screen \
	vdp2-bitmap \
	vdp2-dbgio \
	vdp2-line-scroll \
	vdp2-nbg0 \
	vdp2-normal-bitmap \
	vdp2-rbg0 \
	vdp2-reduction-bitmap \
	vdp2-special-function \
	vdp2-zooming

# Following examples are broken:
#   arp-comm
#   scu-timers
#   vdp1-cube
#   vdp1-st-niccc

ifeq ($(YAUL_OPTION_DEV_CARTRIDGE),$(filter $(YAUL_OPTION_DEV_CARTRIDGE),1 2))
EXAMPLES+= \
	gdb
endif

ifeq ($(YAUL_OPTION_DEV_CARTRIDGE),1)
EXAMPLES+= \
	usb-cart
endif

ifeq ($(YAUL_OPTION_DEV_CARTRIDGE),2)
endif

ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT (install root directory))
endif

ifeq ($(strip $(SILENT)),)
  ECHO=
else
  ECHO=@
endif
export ECHO

.PHONY: all clean

all clean:
	$(ECHO)for example in $(EXAMPLES); do \
		printf -- "[1;36m$@[m [1;32mexamples/$$example[m\n"; \
		($(MAKE) -C $$example $@) || exit $$?; \
	done
