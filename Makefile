.EXPORT_ALL_VARIABLES:

ifeq ($(V),y)
Q := 
else
Q := @
endif

MAKEFLAGS += --no-print-directory --no-builtin-rules

ARCH := x86
# HACK: minor, build - CROSS_COMPILE should be empty (set as enviromental variable). Left filled for convenience right now
CROSS_COMPILE := x86_64-elf-
TOPDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# --------------------------------
# toolchain
# --------------------------------

INCDIR := $(TOPDIR)/include

HOSTCC  := gcc
HOSTCXX := g++
HOSTAR  := ar

CC  := $(CROSS_COMPILE)gcc -I$(INCDIR)
CPP := $(CC) -E
AS  := $(CROSS_COMPILE)as
AR  := $(CROSS_COMPILE)ar
LD  := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
PYTHON := python3
SH := bash

# --------------------------------
# flags
# --------------------------------

HOSTCFLAGS= -O2 -g
HOSTCXXFLAGS= -O2 -g

CFLAGS := -nostartfiles \
		  -nodefaultlibs \
		  -nostdlib \
		  -nostdinc \
		  -ffreestanding \
		  -fshort-wchar \
		  -fno-omit-frame-pointer \
		  -fno-stack-protector \
		  -fno-builtin \
		  -fno-pic -fno-pie \
		  -std=gnu23 \
		  -Wall -Wextra
CPPFLAGS := -D__KERNEL__
ASFLAGS :=
ASPPFLAGS := -D__ASSEMBLY__
LDFLAGS := -static -Bsymbolic -nostdlib

# --------------------------------

ARCHIVES := init/initar.o kernel/kernelar.o \
			mm/mmar.o lib/nyxliba.o fs/fsar.o
LIBS     :=

SUBDIRS := init kernel mm lib fs

PHONY := all do-all vmnyx nyxsubdirs clean distclean symlinks menuconfig config docs tools

all: do-all

ifeq (.config,$(wildcard .config))
include .config
do-all: vmnyx
else
do-all: config
endif

# --------------------------------
# Configuration
# --------------------------------

ifdef CONFIG_DEBUG
CFLAGS += -O0 -g -D__DEBUG
else
CFLAGS += -O2
endif

ifdef CONFIG_KERNEL_TESTS
SUBDIRS += tests
ARCHIVES += tests/kerneltestsar.o
endif

ifdef CONFIG_EXTRA_WARNINGS
CFLAGS += -Wconversion -Wsign-conversion -Wundef -Wcast-align \
          -Wshift-overflow -Wdouble-promotion -Wpedantic
endif

ifdef CONFIG_WARNINGS_AS_ERRORS
CFLAGS += -Werror
endif

CFLAGS += -include $(TOPDIR)/include/generated/autoconf.h
CFLAGS += -include $(TOPDIR)/include/generated/version.h

include arch/$(ARCH)/Makefile

# --------------------------------
# General rules for building the kernel
# --------------------------------

PHONY += vmnyx nyxsubdirs tools

vmnyx: nyxsubdirs $(ARCH_LINK)
	@echo -e "LD $@"
	$(Q)$(LD) $(LDFLAGS) \
		-T $(ARCH_LINK) \
		$(ARCHIVES) \
		$(LIBS) \
		-o $@

nyxsubdirs: include/generated/autoconf.h include/generated/version.h archtargets
	$(Q)set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

tools:
	$(Q)$(MAKE) -C tools

# --------------------------------
# Cleanup rules
# --------------------------------

PHONY += clean distclean

clean: archclean
	find . -type f ! -path './scripts/*' -name '*.[oasd]' -delete
	find . -type d -name "generated" -prune -exec rm -rf {} +
	rm -rf isodir
	rm -f vmnyx image nyxos.iso

distclean: clean
	$(Q)$(MAKE) -C tools clean
	find . -type d -name "tmp" -prune -exec rm -rf {} +
	rm -f .config .config.old include/asi
	rm -rf .cache out scripts/Kconfig/__pycache__

# --------------------------------
# Rules for setting up the project
# --------------------------------

include/generated/version.h: include/generated/version.h.tmp
	@if ! cmp -s $< $@; then cp $< $@; fi

include/generated/version.h.tmp: FORCE
	$(Q)rm -f include/generated/version.h.tmp
	$(Q)$(SH) ./scripts/mkversion.sh include/generated/version.h.tmp

include/generated/autoconf.h: .config
	$(Q)mkdir -p include/generated
	$(Q)$(PYTHON) ./scripts/Kconfig/genconfig.py --header-path include/generated/autoconf.h

PHONY += symlinks menuconfig config docs

symlinks:
	rm -f include/asi include/uapi/asi
	( cd include ; ln -s ../arch/$(ARCH)/include/asi asi ; \
		cd uapi ; ln -s ../../arch/$(ARCH)/include/uapi/asi asi )

menuconfig: symlinks
	$(Q)$(PYTHON) ./scripts/Kconfig/menuconfig.py

config: symlinks
	$(Q)$(PYTHON) ./scripts/Kconfig/oldconfig.py

docs:
	$(Q)mkdir -p out/docs
	$(Q)doxygen

FORCE:

.PHONY: $(PHONY)
