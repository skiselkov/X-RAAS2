# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END

# Copyright 2016 Saso Kiselkov. All rights reserved.

PDFS = UserManual/UserManual.pdf \
    AvionicsIntegrationGuide/AvionicsIntegrationGuide.pdf

all : $(PDFS)

clean :
	rm -f $(PDFS)

%.pdf : %.tex
	(cd $(dir $<) ; pdflatex $(notdir $<) )
	(cd $(dir $<) ; pdflatex $(notdir $<) )
	(cd $(dir $<) ; pdflatex $(notdir $<) )
