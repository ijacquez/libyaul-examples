ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT (install root directory))
endif

include $(YAUL_INSTALL_ROOT)/share/pre.common.mk

SH_PROGRAM:= vdp1-balls
SH_SRCS:= \
	vdp1-balls.c \
	balls.c
ROMDISK_SYMBOLS= root
ROMDISK_DIRS= romdisk

SH_LIBRARIES:=
SH_CFLAGS+= -I. -Wno-error=unused-variable -Wno-error -O2 -save-temps=obj

IP_VERSION:= V1.000
IP_RELEASE_DATE:= 20160101
IP_AREAS:= JTUBKAEL
IP_PERIPHERALS:= JAMKST
IP_TITLE:= VDP1 balls test
IP_MASTER_STACK_ADDR:= 0x06004000
IP_SLAVE_STACK_ADDR:= 0x06002000
IP_1ST_READ_ADDR:= 0x06004000

M68K_PROGRAM:=
M68K_OBJECTS:=

include $(YAUL_INSTALL_ROOT)/share/post.common.mk
