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


#ifndef __SPRUCE_ERROR_H__
#define __SPRUCE_ERROR_H__

#include <glib.h>
#include <errno.h>

G_BEGIN_DECLS

extern GQuark spruce_error_quark;
#define SPRUCE_ERROR_QUARK spruce_error_quark
#define SPRUCE_ERROR SPRUCE_ERROR_QUARK

/* error value is the errno set by a system call */
#define SPRUCE_ERROR_IS_SYSTEM(error) ((error) < SPRUCE_ERROR_GENERIC)

enum {
	SPRUCE_ERROR_GENERIC = 256,
	
	/* service errors */
	SPRUCE_ERROR_SERVICE_UNAVAILABLE,
	SPRUCE_ERROR_SERVICE_NOT_CONNECTED,
	SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
	SPRUCE_ERROR_SERVICE_PROTOCOL_ERROR,
	
	/* store errors */
	SPRUCE_ERROR_STORE_NO_SUCH_FOLDER,
	
	/* folder errors */
	SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
	SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
	SPRUCE_ERROR_FOLDER_READ_ONLY,
	
	/* transport errors */
	SPRUCE_ERROR_TRANSPORT_INVALID_SENDER,
	SPRUCE_ERROR_TRANSPORT_INVALID_RECIPIENT,
	SPRUCE_ERROR_TRANSPORT_NO_RECIPIENTS,
	
	/* provider specific start - this must stay as the last error */
	SPRUCE_ERROR_PROVIDER_SPECIFIC,
};

G_END_DECLS

#endif /* __SPRUCE_ERROR_H__ */
