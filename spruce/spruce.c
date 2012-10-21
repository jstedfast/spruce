/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Spruce
 *  Copyright (C) 1999-2009 Jeffrey Stedfast
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <gmime/gmime.h>

#include "spruce.h"

GQuark spruce_error_quark;

static int initialised = 0;

int
spruce_init (const char *spruce_dir)
{
	if (initialised)
		return 0;
	
	g_mime_init (0);
	
	spruce_error_quark = g_quark_from_static_string ("spruce");
	
	spruce_provider_scan_modules ();
	
	initialised = 1;
	
	return 0;
}


int
spruce_shutdown (void)
{
	if (!initialised)
		return 0;
	
	g_mime_shutdown ();
	
	spruce_provider_shutdown ();
	
	initialised = 0;
	
	return 0;
}
