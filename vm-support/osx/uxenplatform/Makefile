#
# Copyright 2014-2015, Bromium, Inc.
# SPDX-License-Identifier: ISC
#

TOPDIR = ../../..
include $(TOPDIR)/Config.mk

$(DEBUG_ONLY)XCODECONF = Debug
$(REL_ONLY)XCODECONF = Release

XCODEBUILD = xcodebuild -configuration $(XCODECONF)

.PHONY: all dist
all: $(BUILDDIR)/$(XCODECONF)/uxenplatform.kext

.PHONY: $(BUILDDIR)/$(XCODECONF)/uxenplatform.kext
$(BUILDDIR)/$(XCODECONF)/uxenplatform.kext:
	$(XCODEBUILD) build SYMROOT=$(BUILDDIR)

dist: $(BUILDDIR)/$(XCODECONF)/uxenplatform.kext
	$(XCODEBUILD) install DSTROOT=$(DISTDIR_VM_SUPPORT) SYMROOT=$(BUILDDIR)

.PHONY: clean
clean::
	$(_W)echo Cleaning - $(BUILDDIR)
	$(_V)rm -rf $(BUILDDIR)
