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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <gmime/gmime.h>

#include <spruce/spruce-error.h>

#include "spruce-pop-store.h"
#include "spruce-pop-engine.h"
#include "spruce-pop-stream.h"
#include "spruce-pop-folder.h"


struct _POPMessageInfo {
	const char *uid;
	guint32 flags:31;
	guint32 expunged:1;
	size_t octets;
	int seqid;
};

static void spruce_pop_folder_class_init (SprucePOPFolderClass *klass);
static void spruce_pop_folder_init (SprucePOPFolder *folder, SprucePOPFolderClass *klass);
static void spruce_pop_folder_finalize (GObject *object);

static int pop_get_message_count (SpruceFolder *folder); /* * */
static int pop_get_unread_message_count (SpruceFolder *folder); /* * */
static int pop_open (SpruceFolder *folder, GError **err);
static int pop_close (SpruceFolder *folder, gboolean expunge, GError **err);
static int pop_sync (SpruceFolder *folder, gboolean expunge, GError **err);
static int pop_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err);
static GPtrArray *pop_get_uids (SpruceFolder *folder);
static guint32 pop_get_message_flags (SpruceFolder *folder, const char *uid);
static int pop_set_message_flags (SpruceFolder *folder, const char *uids, guint32 flags, guint32 set);
static GMimeMessage *pop_get_message (SpruceFolder *folder, const char *uid, GError **err);


static SpruceFolderClass *parent_class = NULL;


GType
spruce_pop_folder_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SprucePOPFolderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_pop_folder_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SprucePOPFolder),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_pop_folder_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER, "SprucePOPFolder", &info, 0);
	}
	
	return type;
}


static void
spruce_pop_folder_class_init (SprucePOPFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderClass *folder_class = SPRUCE_FOLDER_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER);
	
	object_class->finalize = spruce_pop_folder_finalize;
	
	/* virtual method overload */
	folder_class->get_message_count = pop_get_message_count;
	folder_class->get_unread_message_count = pop_get_unread_message_count;
	folder_class->open = pop_open;
	folder_class->close = pop_close;
	folder_class->sync = pop_sync;
	folder_class->expunge = pop_expunge;
	folder_class->get_uids = pop_get_uids;
	folder_class->get_message_flags = pop_get_message_flags;
	folder_class->set_message_flags = pop_set_message_flags;
	folder_class->get_message = pop_get_message;
}

static void
spruce_pop_folder_init (SprucePOPFolder *pop, SprucePOPFolderClass *klass)
{
	SpruceFolder *folder = (SpruceFolder *) pop;
	
	folder->separator = '/';
	folder->permanent_flags = SPRUCE_MESSAGE_DELETED;
	
	pop->uids = g_ptr_array_new ();
	pop->uid_info = g_hash_table_new (g_str_hash, g_str_equal);
	
	pop->expunge = FALSE;
}

static void
info_free (const char *uid, struct _POPMessageInfo *info, gpointer user_data)
{
	g_free (info);
}

static void
spruce_pop_folder_finalize (GObject *object)
{
	SprucePOPFolder *folder = (SprucePOPFolder *) object;
	int i;
	
	for (i = 0; i < folder->uids->len; i++)
		g_free (folder->uids->pdata[i]);
	g_ptr_array_free (folder->uids, TRUE);
	
	g_hash_table_foreach (folder->uid_info, (GHFunc) info_free, NULL);
	g_hash_table_destroy (folder->uid_info);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


SpruceFolder *
spruce_pop_folder_new (SpruceStore *store, GError **err)
{
	SpruceFolder *folder;
	
	g_return_val_if_fail (SPRUCE_IS_POP_STORE (store), NULL);
	
	folder = g_object_new (SPRUCE_TYPE_POP_FOLDER, NULL);
	spruce_folder_construct (folder, store, NULL, "Inbox", "Inbox");
	folder->type = SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
	folder->supports_searches = FALSE;
	folder->exists = TRUE;
	
	return folder;
}

static int
pop_get_message_count (SpruceFolder *folder)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	
	return pop_folder->uids->len;
}

static int
pop_get_unread_message_count (SpruceFolder *folder)
{
	return 0;
}

struct pop_uidl_t {
	SpruceService *service;
	GError **err;
	
	GPtrArray *uids;
	
	/* for single-message query interface */
	int seqid;
	char *uid;
};

static int
uidl_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	struct pop_uidl_t *uidl = user_data;
	char *p, *linebuf = NULL;
	int seqid, err;
	size_t len;
	
	if (pc->status == SPRUCE_POP_COMMAND_ERR) {
		g_set_error (uidl->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Cannot get UID listing from POP server %s: %s"),
			     uidl->service->url->host, line);
		return -1;
	}
	
	if (uidl->seqid > 0) {
		/* parse singelton UID response */
		if ((seqid = strtoul (line, &p, 10)) != uidl->seqid || *p != ' ') {
			g_set_error (uidl->err, SPRUCE_ERROR, errno,
				     _("Unexpected POP UIDL response for message %d: %s"),
				     uidl->seqid, line);
			
			return -1;
		}
		
		while (*p == ' ' || *p == '\t')
			p++;
		
		if (*p == '\0') {
			g_set_error (uidl->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Unexpected POP UIDL response for message %d: %s"),
				     uidl->seqid, line);
			
			return -1;
		}
		
		uidl->uid = g_strdup (p);
		
		return 0;
	}
	
	/* read/parse multi-line UID response */
	do {
		g_free (linebuf);
		
		/* read a UID line */
		if ((err = spruce_pop_engine_get_line (engine, &linebuf, &len)) != 0) {
			g_set_error (uidl->err, SPRUCE_ERROR, err > 0 ? err : SPRUCE_ERROR_GENERIC,
				     _("Cannot get UID listing from POP server %s: %s"),
				     uidl->service->url->host,
				     err > 0 ? g_strerror (err) : _("Unknown error"));
			
			return -1;
		}
		
		/* check for sentinel */
		if (!strcmp (linebuf, "."))
			break;
		
		if ((seqid = strtoul (linebuf, &p, 10)) != (uidl->uids->len + 1) || *p != ' ') {
		unexpected:
			g_set_error (uidl->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Unexpected UIDL response from POP server %s for message %d: %s"),
				     uidl->service->url->host, uidl->uids->len + 1, linebuf);
			
			g_free (linebuf);
			
			return -1;
		}
		
		while (*p == ' ' || *p == '\t')
			p++;
		
		if (*p == '\0')
			goto unexpected;
		
		g_ptr_array_add (uidl->uids, g_strdup (p));
	} while (1);
	
	g_free (linebuf);
	
	return 0;
}

static int
scan_uidl (SpruceFolder *folder, SprucePOPEngine *engine, GError **err)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	struct pop_uidl_t uidl;
	SprucePOPCommand *pc;
	int id, retval, i;
	
	uidl.service = (SpruceService *) folder->store;
	uidl.uids = pop_folder->uids;
	uidl.uid = NULL;
	uidl.err = err;
	uidl.seqid = 0;
	
	pc = spruce_pop_engine_queue (engine, uidl_cmd, &uidl, "UIDL\r\n");
	while ((id = spruce_pop_engine_iterate (engine)) < pc->id && id != -1)
		;
	
	if (id == -1 || pc->retval == -1) {
		if (id == -1) {
			g_set_error (err, SPRUCE_ERROR, pc->error ? pc->error : SPRUCE_ERROR_GENERIC,
				     _("Cannot get UID listing from POP server %s: %s"),
				     uidl.service->url->host, pc->error ? g_strerror (pc->error) : _("Unknown"));
		}
		
		retval = pc->status == SPRUCE_POP_COMMAND_ERR ? pc->status : SPRUCE_POP_COMMAND_PROTOCOL_ERROR;
		
		spruce_pop_command_free (engine, pc);
		
		/* free any uids gotten */
		for (i = 0; i < uidl.uids->len; i++)
			g_free (uidl.uids->pdata[i]);
		g_ptr_array_set_size (uidl.uids, 0);
		
		return retval;
	}
	
	spruce_pop_command_free (engine, pc);
	
	for (i = 0; i < uidl.uids->len; i++) {
		struct _POPMessageInfo *info;
		
		info = g_new0 (struct _POPMessageInfo, 1);
		info->uid = uidl.uids->pdata[i];
		info->octets = (size_t) -1;
		info->seqid = i;
		
		g_hash_table_insert (pop_folder->uid_info, uidl.uids->pdata[i], info);
	}
	
	return SPRUCE_POP_COMMAND_OK;
}

struct pop_list_t {
	SpruceService *service;
	GError **err;
	
	GPtrArray *infos;
};

static int
list_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	struct pop_list_t *list = user_data;
	struct _POPMessageInfo *info;
	char *p, *linebuf = NULL;
	int seqid, err;
	size_t len;
	
	if (pc->status == SPRUCE_POP_COMMAND_ERR) {
		g_set_error (list->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Cannot get UID listing from POP server %s: %s"),
			     list->service->url->host, line);
		return -1;
	}
	
	/* read/parse multi-line UID response */
	do {
		g_free (linebuf);
		
		/* read a UID line */
		if ((err = spruce_pop_engine_get_line (engine, &linebuf, &len)) != 0) {
			g_set_error (list->err, SPRUCE_ERROR, err > 0 ? err : SPRUCE_ERROR_GENERIC,
				     _("Cannot get UID listing from POP server %s: %s"),
				     list->service->url->host,
				     err > 0 ? g_strerror (err) : _("Unknown error"));
			
			return -1;
		}
		
		if (!strcmp (linebuf, "."))
			break;
		
		if ((seqid = strtoul (linebuf, &p, 10)) != (list->infos->len + 1) || *p != ' ') {
		unexpected:
			g_set_error (list->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Unexpected LIST response from POP server %s for message %d: %s"),
				     list->service->url->host, list->infos->len + 1, linebuf);
			
			g_free (linebuf);
			
			return -1;
		}
		
		while (*p == ' ' || *p == '\t')
			p++;
		
		if (!(*p >= '1' && *p <= '9'))
			goto unexpected;
		
		info = g_new0 (struct _POPMessageInfo, 1);
		info->octets = strtoul (p, NULL, 10);
		info->seqid = seqid;
		
		g_ptr_array_add (list->infos, info);
	} while (1);
	
	g_free (linebuf);
	
	return 0;
}

static int
scan_list (SpruceFolder *folder, SprucePOPEngine *engine, GError **err)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	struct pop_list_t list;
	SprucePOPCommand *pc;
	int id, retval, i;
	
	list.service = (SpruceService *) folder->store;
	list.infos = g_ptr_array_new ();
	list.err = err;
	
	pc = spruce_pop_engine_queue (engine, list_cmd, &list, "LIST\r\n");
	while ((id = spruce_pop_engine_iterate (engine)) < pc->id && id != -1)
		;
	
	if (id == -1 || pc->retval == -1) {
		if (id == -1) {
			g_set_error (err, SPRUCE_ERROR, pc->error ? pc->error : SPRUCE_ERROR_GENERIC,
				     _("Cannot get UID listing from POP server %s: %s"),
				     list.service->url->host, pc->error ? g_strerror (pc->error) : _("Unknown"));
		}
		
		retval = pc->status == SPRUCE_POP_COMMAND_ERR ? pc->status : SPRUCE_POP_COMMAND_PROTOCOL_ERROR;
		
		spruce_pop_command_free (engine, pc);
		
		/* free any info's gotten */
		for (i = 0; i < list.infos->len; i++)
			g_free (list.infos->pdata[i]);
		g_ptr_array_free (list.infos, TRUE);
		
		return retval;
	}
	
	spruce_pop_command_free (engine, pc);
	
	g_ptr_array_set_size (pop_folder->uids, list.infos->len);
	for (i = 0; i < list.infos->len; i++) {
		struct _POPMessageInfo *info;
		
		info = list.infos->pdata[i];
		
		pop_folder->uids->pdata[i] = g_strdup_printf ("%u:%zu", info->seqid, info->octets);
		info->uid = pop_folder->uids->pdata[i];
		
		g_hash_table_insert (pop_folder->uid_info, pop_folder->uids->pdata[i], info);
	}
	
	g_ptr_array_free (list.infos, TRUE);
	
	return SPRUCE_POP_COMMAND_OK;
}

static int
pop_open (SpruceFolder *folder, GError **err)
{
	SprucePOPEngine *engine;
	int ret;
	
	engine = ((SprucePOPStore *) folder->store)->engine;
	
	if (engine->capa & SPRUCE_POP_CAPA_UIDL) {
		if (scan_uidl (folder, engine, err) == SPRUCE_POP_COMMAND_OK)
			return 0;
		
		return -1;
	}
	
	if (!(engine->capa & SPRUCE_POP_CAPA_PROBED_UIDL)) {
		if ((ret = scan_uidl (folder, engine, err)) == SPRUCE_POP_COMMAND_OK)
			return 0;
		
		if (ret != SPRUCE_POP_COMMAND_ERR)
			return -1;
		
		/* okay, server doesn't support UIDL... */
		engine->capa |= SPRUCE_POP_CAPA_PROBED_UIDL;
		g_clear_error (err);
	}
	
#ifdef USE_TOP_MD5SUM_FOR_UIDS
	if (engine->capa & SPRUCE_POP_CAPA_TOP) {
		if (scan_top (folder, engine, err) == SPRUCE_POP_COMMAND_OK)
			return 0;
		
		return -1;
	}
	
	if (!(engine->capa & SPRUCE_POP_CAPA_PROBED_TOP)) {
		if ((ret = scan_top (folder, engine, err)) == SPRUCE_POP_COMMAND_OK)
			return 0;
		
		if (ret != SPRUCE_POP_COMMAND_ERR)
			return -1;
		
		/* okay, server doesn't support TOP... */
		engine->capa |= SPRUCE_POP_CAPA_PROBED_TOP;
		g_clear_error (err);
	}
#endif
	
	if (scan_list (folder, engine, err) == SPRUCE_POP_COMMAND_OK)
		return 0;
	
	return -1;
}

static void
pop_dele (const char *uid, struct _POPMessageInfo *info, GPtrArray *uids)
{
	if (info->expunged)
		g_ptr_array_add (uids, GINT_TO_POINTER (info->seqid));
}

static int
pop_close (SpruceFolder *folder, gboolean expunge, GError **err)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	SprucePOPEngine *engine;
	SprucePOPCommand *pc;
	GPtrArray *dele;
	int id, i;
	
	if (!pop_folder->expunge && !expunge)
		goto done;
	
	if (expunge)
		pop_expunge (folder, NULL, err);
	
	engine = ((SprucePOPStore *) folder->store)->engine;
	
	dele = g_ptr_array_new ();
	g_hash_table_foreach (pop_folder->uid_info, (GHFunc) pop_dele, dele);
	if (dele->len > 0) {
		for (i = 0; i < dele->len; i++) {
			int seqid = GPOINTER_TO_INT (dele->pdata[i]);
			
			pc = spruce_pop_engine_queue (engine, NULL, NULL, "DELE %d\r\n", seqid);
			dele->pdata[i] = pc;
		}
		
		while ((id = spruce_pop_engine_iterate (engine)) < pc->id && id != -1)
			;
		
		for (i = 0; i < dele->len; i++)
			spruce_pop_command_free (engine, dele->pdata[i]);
	}
	
	g_ptr_array_free (dele, TRUE);
	
 done:
	
	return 0;
}

static int
pop_sync (SpruceFolder *folder, gboolean expunge, GError **err)
{
	if (expunge)
		return pop_expunge (folder, NULL, err);
	
	return 0;
}

static void
pop_expunge_info (const char *uid, struct _POPMessageInfo *info, gpointer user_data)
{
	if (info->flags & SPRUCE_MESSAGE_DELETED) {
		/* consider this message expunged */
		info->expunged = TRUE;
	}
}

static int
pop_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	struct _POPMessageInfo *info;
	guint i;
	
	if (SPRUCE_FOLDER_CLASS (parent_class)->expunge (folder, uids, err) == -1)
		return -1;
	
	if (uids) {
		for (i = 0; i < uids->len; i++) {
			if ((info = g_hash_table_lookup (pop_folder->uid_info, uids->pdata[i])) &&
			    (info->flags & SPRUCE_MESSAGE_DELETED))
				info->expunged = TRUE;
		}
	} else {
		g_hash_table_foreach (pop_folder->uid_info, (GHFunc) pop_expunge_info, NULL);
	}
	
	pop_folder->expunge = TRUE;
	
	return 0;
}

static void
pop_get_uid (const char *uid, struct _POPMessageInfo *info, GPtrArray *uids)
{
	if (!info->expunged)
		g_ptr_array_add (uids, g_strdup (uid));
}

static GPtrArray *
pop_get_uids (SpruceFolder *folder)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	GPtrArray *uids;
	
	uids = g_ptr_array_new ();
	g_hash_table_foreach (pop_folder->uid_info, (GHFunc) pop_get_uid, uids);
	
	if (uids->len == 0) {
		g_ptr_array_free (uids, TRUE);
		return NULL;
	}
	
	return uids;
}

static guint32
pop_get_message_flags (SpruceFolder *folder, const char *uid)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	struct _POPMessageInfo *info;
	
	if ((info = g_hash_table_lookup (pop_folder->uid_info, uid)))
		return info->flags & SPRUCE_MESSAGE_DELETED;
	
	return 0;
}

static int
pop_set_message_flags (SpruceFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	struct _POPMessageInfo *info;
	guint32 new;
	
	if ((info = g_hash_table_lookup (pop_folder->uid_info, uid))) {
		new = (info->flags & ~flags) | (set & flags);
		
		if (info->flags != new)
			info->flags = new | SPRUCE_MESSAGE_DIRTY;
		
		return 0;
	}
	
	return -1;
}

struct pop_retr_t {
	SpruceFolder *folder;
	GMimeStream *stream;
	const char *uid;
	GError **err;
};

static int
retr_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	struct pop_retr_t *retr = user_data;
	
	if (pc->status == SPRUCE_POP_COMMAND_ERR) {
		g_set_error (retr->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Cannot get message %s from folder `%s': %s"),
			     retr->uid, retr->folder->full_name, line);
		return -1;
	}
	
	spruce_pop_stream_set_mode (engine->stream, SPRUCE_POP_STREAM_DATA);
	g_mime_stream_write_to_stream ((GMimeStream *) engine->stream, retr->stream);
	spruce_pop_stream_set_mode (engine->stream, SPRUCE_POP_STREAM_LINE);
	
	if (!engine->stream->eod) {
		/* didn't encounter an EOD marker */
		if (errno == 0 && engine->stream->disconnected)
			errno = ECONNRESET;
		
		g_set_error (retr->err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
			     _("Cannot get message %s from folder `%s': %s"), retr->uid,
			     retr->folder->full_name, errno ? g_strerror (errno) : _("Unknown"));
		return -1;
	}
	
	return 0;
}

static GMimeMessage *
pop_get_message (SpruceFolder *folder, const char *uid, GError **err)
{
	SprucePOPFolder *pop_folder = (SprucePOPFolder *) folder;
	SprucePOPStore *pop_store = (SprucePOPStore *) folder->store;
	struct _POPMessageInfo *info;
	struct pop_retr_t retr;
	GMimeMessage *message;
	SprucePOPCommand *pc;
	GMimeParser *parser;
	int id;
	
	if (!(info = g_hash_table_lookup (pop_folder->uid_info, uid)) || info->expunged) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': no such message"),
			     uid, folder->full_name);
		
		return NULL;
	}
	
	retr.stream = g_mime_stream_mem_new ();
	retr.folder = folder;
	retr.uid = uid;
	retr.err = err;
	
	pc = spruce_pop_engine_queue (pop_store->engine, retr_cmd, &retr, "RETR %d\r\n", info->seqid);
	while ((id = spruce_pop_engine_iterate (pop_store->engine)) < pc->id && id != -1)
		;
	
	if (id == -1 || pc->retval == -1) {
		if (id == -1) {
			/* need to set our own error */
			g_set_error (err, SPRUCE_ERROR, pc->error ? pc->error : SPRUCE_ERROR_GENERIC,
				     _("Cannot get message %s from folder `%s': %s"),
				     uid, folder->full_name, pc->error ? g_strerror (pc->error) : _("Unknown"));
		}
		
		spruce_pop_command_free (pop_store->engine, pc);
		g_object_unref (retr.stream);
		return NULL;
	}
	
	spruce_pop_command_free (pop_store->engine, pc);
	
	g_mime_stream_reset (retr.stream);
	
	parser = g_mime_parser_new ();
	g_mime_parser_init_with_stream (parser, retr.stream);
	g_object_unref (retr.stream);
	
	if (!(message = g_mime_parser_construct_message (parser))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': internal parser error"),
			     uid, folder->full_name);
	}
	
	g_object_unref (parser);
	
	return message;
}
