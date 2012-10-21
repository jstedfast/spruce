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


#ifndef __MAILX_SESSION_H__
#define __MAILX_SESSION_H__

#include <spruce/spruce-session.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define MAILX_TYPE_SESSION            (mailx_session_get_type ())
#define MAILX_SESSION(obj)            (MAILX_CHECK_CAST ((obj), MAILX_TYPE_SESSION, MailxSession))
#define MAILX_SESSION_CLASS(klass)    (MAILX_CHECK_CLASS_CAST ((klass), MAILX_TYPE_SESSION, MailxSessionClass))
#define MAILX_IS_SESSION(obj)         (MAILX_CHECK_TYPE ((obj), MAILX_TYPE_SESSION))
#define MAILX_IS_SESSION_CLASS(klass) (MAILX_CHECK_CLASS_TYPE ((klass), MAILX_TYPE_SESSION))
#define MAILX_SESSION_GET_CLASS(obj)  (MAILX_CHECK_GET_CLASS ((obj), MAILX_TYPE_SESSION, MailxSessionClass))

typedef struct _MailxSession MailxSession;
typedef struct _MailxSessionClass MailxSessionClass;

struct _MailxSession {
	SpruceSession parent_object;
	
	struct _MailxSessionPrivate *priv;
};

struct _MailxSessionClass {
	SpruceSessionClass parent_class;
	
};


GType mailx_session_get_type (void);

SpruceSession *mailx_session_new (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MAILX_SESSION_H__ */
