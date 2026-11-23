# SPDX-License-Identifier: GPL-2.0
#
# Out-of-tree build of the canxl_eth module (Linux 6.12 and later)
#
# make                     : build canxl_eth.ko for the running kernel
# make KDIR=<dir>          : build canxl_eth.ko for the kernel build tree <dir>
# sudo make install        : install canxl_eth.ko in /lib/modules/<version>/updates
# sudo make uninstall      : remove canxl_eth.ko from /lib/modules/<version>/updates
# sudo make dkms-install   : install or update the sources with DKMS (auto rebuild)
# sudo make dkms-uninstall : remove the DKMS installation
#
# The Kbuild part (module and object files) is located in the file Kbuild.
#

KDIR ?= /lib/modules/$(shell uname -r)/build

# kernel release of the kernel build tree KDIR
KRELEASE := $(shell cat $(KDIR)/include/config/kernel.release 2>/dev/null)
ifeq ($(KRELEASE),)
KRELEASE := $(shell uname -r)
endif
MODDIR := $(INSTALL_MOD_PATH)/lib/modules/$(KRELEASE)/updates
DEPMOD := depmod $(if $(INSTALL_MOD_PATH),-b $(INSTALL_MOD_PATH)) -a $(KRELEASE)

PACKAGE_NAME := canxl_eth
PACKAGE_VERSION := $(shell sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf)
DKMS_SRC := /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
DKMS_FILES := Kbuild Makefile dkms.conf compat.h xl_eth.c include

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

# depmod is called explicitly as kbuild skips depmod without System.map
# (e.g. with the Debian linux-headers packages)
install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) INSTALL_MOD_DIR=updates modules_install
	$(DEPMOD)

uninstall:
	rm -f $(MODDIR)/$(PACKAGE_NAME).ko $(MODDIR)/$(PACKAGE_NAME).ko.*
	$(DEPMOD)

dkms-check:
	@command -v dkms >/dev/null 2>&1 || \
		{ echo "dkms not found: install it first (e.g. 'apt install dkms')"; exit 1; }

# an existing DKMS installation of the same version is replaced
dkms-install: dkms-check
	@if [ -n "$$(dkms status -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION))" ]; then \
		echo "dkms remove -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION) --all"; \
		dkms remove -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION) --all; \
	fi
	rm -rf $(DKMS_SRC)
	mkdir -p $(DKMS_SRC)
	cp -r $(DKMS_FILES) $(DKMS_SRC)/
	dkms add -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION)
	dkms install -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION)

dkms-uninstall: dkms-check
	-dkms remove -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION) --all
	rm -rf $(DKMS_SRC)

.PHONY: all clean install uninstall dkms-check dkms-install dkms-uninstall
