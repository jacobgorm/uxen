#
# Copyright 2012-2016, Bromium, Inc.
# SPDX-License-Identifier: ISC
#

BUILDDIR_default = obj
SRCDIR ?= .
TOPDIR = $(abspath $(SRCDIR)/../..)
include $(TOPDIR)/Config.mk

$(call include_lib,LIBVHD,../libvhd)
$(call include_lib,LIBXC,../libxc)
$(call include_lib,LIBUXENCTL,../uxenctl)
$(call include_lib,YAJL,../yajl)
ifeq (,$(UXENDM_VNCSERVER))
$(call include_lib,LIBVNCSERVER,../libvncserver)
endif

ifeq (,$(MAKENOW))

SRCROOT = $(abspath $(TOPDIR)/dm)

VPATH = $(SRCROOT)

include $(SRCROOT)/Makefile.dm

endif # MAKENOW
