#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# uts/intel/pscmm/drm/Makefile
#
# Copyright 2010 PathScale Inc.  All rights reserved.
#  Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
#	This makefile drives the production of the DRM (Direct Rendering
#	Manager) common misc module.
#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../../..

#
#	Define the module and object file sets.
#
MODULE		= nouveau_drm
OBJECTS		= $(NOUVEAU_DRM_OBJS:%=$(DRM_OBJS_DIR)/%)
LINTS		= $(NOUVEAU_DRM_OBJS:%.o=$(DRM_LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_MISC_DIR)/$(MODULE)
DRM_SRC		= $(UTSBASE)/intel/pscmm/drm
GFX_DIR		= $(UTSBASE)/i86pc/io/gfx_private


INC_PATH	+= -I$(UTSBASE)/intel/pscmm/drm -I$(GFX_DIR)

# Dependency
LDFLAGS		+= -dy -Nmisc/agpmaster -Nmisc/gfx_private

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

NOUVEAU_DRM_OBJS += drm_sunmod.o drm_kstat.o drm_agpsupport.o \
            drm_auth.o drm_bufs.o drm_context.o drm_dma.o \
            drm_drawable.o drm_drv.o drm_fops.o drm_ioctl.o drm_irq.o \
            drm_lock.o drm_memory.o drm_msg.o drm_pci.o drm_scatter.o \
            drm_cache.o drm_gem.o drm_mm.o drm_io32.o \
            drm_modes.o drm_crtc_helper.o drm_crtc.o drm_edid.o \
            drm_sun_idr.o drm_sun_pci.o drm_sun_i2c.o drm_stub.o \
            drm_sysfs.o

MISC_KMODS     += nouveau_drm

#
#	Define targets
#
ALL_TARGET	= $(BINARY)
LINT_TARGET	= $(MODULE).lint
INSTALL_TARGET	= $(BINARY) $(ROOTMODULE)

#
#	Default build targets.
#
.KEEP_STATE:

def:		$(DEF_DEPS)

all:		$(ALL_DEPS)

clean:		$(CLEAN_DEPS)

clobber:	$(CLOBBER_DEPS)

lint:		$(LINT_DEPS)

modlintlib:	$(MODLINTLIB_DEPS)

clean.lint:	$(CLEAN_LINT_DEPS)

install:	$(INSTALL_DEPS)

#
#	Include common targets.
#
include $(UTSBASE)/intel/Makefile.targ

$(DRM_OBJS_DIR)/%.o:		$(UTSBASE)/intel/pscmm/drm/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)

$(DRM_LINTS_DIR)/%.ln:		$(UTSBASE)/intel/pscmm/drm/%.c
	@($(LHEAD) $(LINT.c) $< $(LTAIL))
