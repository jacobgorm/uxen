#
# Copyright 2012-2019, Bromium, Inc.
# SPDX-License-Identifier: ISC
#

TOPDIR = ../..
include $(TOPDIR)/Config.mk

SUBDIRS  =
SUBDIRS += uxenvmlib
SUBDIRS += uxenv4vlib
SUBDIRS += uxendisp/xddm
SUBDIRS += uxenmouse
SUBDIRS += uxenaudio
SUBDIRS += uxenclipboard
SUBDIRS += uxenevent
SUBDIRS += uxendmpdrv
SUBDIRS += uxenkmdod
SUBDIRS += uxenplatform
SUBDIRS += uxensvc
SUBDIRS += elevate
SUBDIRS += uxenv4vguest
SUBDIRS += uxenstor
SUBDIRS += uxennet
SUBDIRS += uxennullnet
SUBDIRS += uxenpatcher
SUBDIRS += uxensf
SUBDIRS += uxenhid
SUBDIRS += installer

TARGETS = all dist

.PHONY: $(TARGETS)

$(TARGETS): % : subdirs-%

.PHONY: clean
clean::
	$(_W)echo Cleaning - $(BUILDDIR)
	$(_V)rm -rf $(BUILDDIR)

.PHONY: tests
tests:: subdirs-tests
