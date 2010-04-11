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

# Copyright 2010 PathScale Inc.  All rights reserved.
#  Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# pscmm/nouveau/Makefile
#
#	This makefile drives the production of nouveau graphics device driver,
#	which supports the DRI (Direct Rendering Infrastructure), with the help
#	of drm common misc module.
#
#	intel platform dependent
#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE = ../../..

#
#	Define the module and object file sets.
#
MODULE		= nouveau
OBJECTS		= $(NOUVEAU_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(NOUVEAU_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_DRV_DIR)/$(MODULE)

# nouveau driver depends on drm, agpmaster and gfx_private misc modules
INC_PATH        += -I$(UTSBASE)/intel/pscmm -I$(UTSBASE)/pscmm/nouveau

#
#	dependency
LDFLAGS	+= -dy -Nmisc/drm

#
#	Include common rules.
#
include $(UTSBASE)/intel/Makefile.intel

#nouveau files
NOUVEAU_OBJS += nouveau_channel.o nouveau_dma.o nouveau_drv.o nouveau_irq.o \
		nouveau_mem.o nouveau_notifier.o nouveau_object.o nouveau_pscmm.o \
		nouveau_sgdma.o nouveau_state.o nv04_fifo.o nv04_timer.o nv50_fb.o \
		nv50_fifo.o nv50_graph.o nv50_instmem.o nv50_mc.o

DRV_KMODS       += nouveau

#
#	Re-define targets
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

$(OBJS_DIR)/%.o:		$(UTSBASE)/intel/pscmm/nouveau/%.c
	$(COMPILE.c) -o $@ $<
	$(CTFCONVERT_O)

$(LINTS_DIR)/%.ln:		$(UTSBASE)/intel/pscmm/nouveau/%.c
	@($(LHEAD) $(LINT.c) $< $(LTAIL))
