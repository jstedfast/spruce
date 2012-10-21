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

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <gmime/gmime-utils.h>
#include <gmime/gmime-multipart.h>
#include <gmime/gmime-multipart-signed.h>
#include <gmime/gmime-multipart-encrypted.h>
#include <gmime/gmime-stream-fs.h>
#include <gmime/gmime-stream-null.h>
#include <gmime/gmime-stream-buffer.h>

#include "spruce-folder-summary.h"
#include "spruce-file-utils.h"


struct _SpruceFolderSummaryPrivate {
	char *filename;
};

static void spruce_folder_summary_class_init (SpruceFolderSummaryClass *klass);
static void spruce_folder_summary_init (SpruceFolderSummary *summary, SpruceFolderSummaryClass *klass);
static void spruce_folder_summary_finalize (GObject *object);

static int header_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int header_save (SpruceFolderSummary *summary, GMimeStream *stream);
static int summary_load (SpruceFolderSummary *summary);
static int summary_save (SpruceFolderSummary *summary);
static int summary_unload (SpruceFolderSummary *summary);
static void summary_add (SpruceFolderSummary *summary, SpruceMessageInfo *info);
static SpruceMessageInfo *message_info_new (SpruceFolderSummary *summary);
static SpruceMessageInfo *message_info_new_from_message (SpruceFolderSummary *summary,
							 GMimeMessage *message);
static SpruceMessageInfo *message_info_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int message_info_save (SpruceFolderSummary *summary, GMimeStream *stream, SpruceMessageInfo *info);
static void message_info_free (SpruceFolderSummary *summary, SpruceMessageInfo *info);
static char *next_uid_string (SpruceFolderSummary *summary);


static GObjectClass *parent_class = NULL;


GType
spruce_folder_summary_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceFolderSummaryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_folder_summary_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceFolderSummary),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_folder_summary_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceFolderSummary", &info, 0);
	}
	
	return type;
}


static void
spruce_folder_summary_class_init (SpruceFolderSummaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_folder_summary_finalize;
	
	klass->header_load = header_load;
	klass->header_save = header_save;
	klass->summary_load = summary_load;
	klass->summary_save = summary_save;
	klass->summary_unload = summary_unload;
	klass->add = summary_add;
	klass->message_info_new = message_info_new;
	klass->message_info_new_from_message = message_info_new_from_message;
	klass->message_info_load = message_info_load;
	klass->message_info_save = message_info_save;
	klass->message_info_free = message_info_free;
	klass->next_uid_string = next_uid_string;
}

static void
spruce_folder_summary_init (SpruceFolderSummary *summary, SpruceFolderSummaryClass *klass)
{
	summary->priv = g_new (struct _SpruceFolderSummaryPrivate, 1);
	summary->priv->filename = NULL;
	
	summary->version = 0;
	summary->flags = 0;
	summary->nextuid = 0;
	summary->count = 0;
	summary->timestamp = 0;
	summary->message_info_size = sizeof (SpruceMessageInfo);
	summary->messages = g_ptr_array_new ();
	summary->messages_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
spruce_folder_summary_finalize (GObject *object)
{
	SpruceFolderSummary *summary = (SpruceFolderSummary *) object;
	GPtrArray *array;
	int i;
	
	array = summary->messages;
	for (i = 0; i < array->len; i++)
		SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_free (summary, array->pdata[i]);
	
	g_ptr_array_free (summary->messages, TRUE);
	g_hash_table_destroy (summary->messages_hash);
	
	g_free (summary->priv->filename);
	g_free (summary->priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * spruce_folder_summary_new:
 *
 * Create a new #SpruceFolderSummary.
 *
 * Returns: a new #SpruceFolderSummary.
 **/
SpruceFolderSummary *
spruce_folder_summary_new (void)
{
	SpruceFolderSummary *summary;
	
	summary = g_object_new (SPRUCE_TYPE_FOLDER_SUMMARY, NULL);
	
	return summary;
}


/**
 * spruce_folder_summary_get_filename:
 * @summary: a #SpruceFolderSummary
 *
 * Gets the summary filename.
 *
 * Returns: the summary filename or %NULL if not set.
 **/
const char *
spruce_folder_summary_get_filename (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), NULL);
	
	return summary->priv->filename;
}


/**
 * spruce_folder_summary_set_filename:
 * @summary: a #SpruceFolderSummary
 * @filename: filename
 *
 * Sets the summary filename.
 **/
void
spruce_folder_summary_set_filename (SpruceFolderSummary *summary, const char *filename)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	
	g_free (summary->priv->filename);
	summary->priv->filename = g_strdup (filename);
}


/**
 * spruce_folder_summary_get_next_uid:
 * @summary: a #SpruceFolderSummary
 *
 * Gets the next available uid as a 32bit unsigned integer value.
 *
 * Returns: the next available uid.
 **/
guint32
spruce_folder_summary_next_uid (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), 0);
	
	return summary->nextuid;
}


static char *
next_uid_string (SpruceFolderSummary *summary)
{
	return g_strdup_printf ("%lu", (unsigned long int) summary->nextuid++);
}


/**
 * spruce_folder_summary_uid_string:
 * @summary: a #SpruceFolderSummary
 *
 * Gets the next available uid as a string.
 *
 * Returns: the next available uid as a string.
 **/
char *
spruce_folder_summary_uid_string (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), NULL);
	
	return SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->next_uid_string (summary);
}


static int
header_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	time_t timestamp;
	guint32 value;
	
	if (spruce_file_util_decode_uint32 (stream, &value) == -1)
		return -1;
	
	if (summary->version != value)
		return -1;
	
	if (spruce_file_util_decode_uint32 (stream, &value) == -1)
		return -1;
	
	summary->flags = value;
	
	if (spruce_file_util_decode_uint32 (stream, &value) == -1)
		return -1;
	
	summary->nextuid = value;
	
	if (spruce_file_util_decode_time_t (stream, &timestamp) == -1)
		return -1;
	
	summary->timestamp = timestamp;
	
	if (spruce_file_util_decode_uint32 (stream, &value) == -1)
		return -1;
	
	summary->count = value;
	
	if (spruce_file_util_decode_uint32 (stream, &value) == -1)
		return -1;
	
	summary->unread = value;
	
	if (spruce_file_util_decode_uint32 (stream, &value) == -1)
		return -1;
	
	summary->deleted = value;
	
	return 0;
}


/**
 * spruce_folder_summary_header_load:
 * @summary: a #SpruceFolderSummary
 *
 * Loads the summary header from disk.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_summary_header_load (SpruceFolderSummary *summary)
{
	GMimeStream *stream, *buffered;
	int ret, fd;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), -1);
	g_return_val_if_fail (summary->priv->filename != NULL, -1);
	
	if ((fd = open (summary->priv->filename, O_RDONLY)) == -1)
		return -1;
	
	stream = g_mime_stream_fs_new (fd);
	buffered = g_mime_stream_buffer_new (stream, GMIME_STREAM_BUFFER_BLOCK_READ);
	g_object_unref (stream);
	
	ret = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->header_load (summary, buffered);
	
	g_object_unref (buffered);
	
	return ret;
}


static int
header_save (SpruceFolderSummary *summary, GMimeStream *stream)
{
	guint32 i, count, unread = 0, deleted = 0;
	
	summary->timestamp = time (NULL);
	
	if (spruce_file_util_encode_uint32 (stream, summary->version) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, summary->flags) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, summary->nextuid) == -1)
		return -1;
	
	if (spruce_file_util_encode_time_t (stream, summary->timestamp) == -1)
		return -1;
	
	count = spruce_folder_summary_count (summary);
	for (i = 0; i < count; i++) {
		SpruceMessageInfo *info;
		
		if (!(info = spruce_folder_summary_index (summary, i))) {
			count--;
			continue;
		}
		
		if ((info->flags & SPRUCE_MESSAGE_SEEN) == 0)
			unread++;
		if ((info->flags & SPRUCE_MESSAGE_DELETED) == 0)
			deleted++;
		
		spruce_folder_summary_info_unref (summary, info);
	}
	
	if (spruce_file_util_encode_uint32 (stream, count) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, unread) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, deleted) == -1)
		return -1;
	
	return 0;
}


/**
 * spruce_folder_summary_header_save:
 * @summary: a #SpruceFolderSummary
 *
 * Saves the summary header to disk.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_summary_header_save (SpruceFolderSummary *summary)
{
	GMimeStream *stream, *buffered;
	int ret, fd;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), -1);
	g_return_val_if_fail (summary->priv->filename != NULL, -1);
	
	if ((fd = open (summary->priv->filename, O_WRONLY | O_CREAT, 0666)) == -1)
		return -1;
	
	stream = g_mime_stream_fs_new (fd);
	buffered = g_mime_stream_buffer_new (stream, GMIME_STREAM_BUFFER_BLOCK_READ);
	g_object_unref (stream);
	
	ret = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->header_save (summary, buffered);
	
	g_object_unref (buffered);
	
	return ret;
}


static SpruceMessageInfo *
message_info_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceMessageInfo *info;
	guint32 count, i;
	
	info = spruce_folder_summary_info_new (summary);
	
	if (spruce_file_util_decode_string (stream, &info->sender) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->from) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->reply_to) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->to) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->cc) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->bcc) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->subject) == -1)
		goto exception;
	
	if (spruce_file_util_decode_time_t (stream, &info->date_sent) == -1)
		goto exception;
	
	if (spruce_file_util_decode_time_t (stream, &info->date_received) == -1)
		goto exception;
	
	if (spruce_file_util_decode_string (stream, &info->uid) == -1)
		goto exception;
	
	/* decode Message-Id */
	if (spruce_file_util_decode_uint32 (stream, &info->message_id.id.part.hi) == -1)
		goto exception;
	if (spruce_file_util_decode_uint32 (stream, &info->message_id.id.part.lo) == -1)
		goto exception;
	
	/* decode References */
	if (spruce_file_util_decode_uint32 (stream, &count) == -1)
		goto exception;
	if (count > 0) {
		info->references = g_try_malloc (sizeof (SpruceSummaryReferences) + sizeof (SpruceSummaryMessageID) * (count - 1));
		if (info->references == NULL)
			goto exception;
		
		info->references->count = count;
		for (i = 0; i < count; i++) {
			if (spruce_file_util_decode_uint32 (stream, &info->references->references[i].id.part.hi) == -1)
				goto exception;
			if (spruce_file_util_decode_uint32 (stream, &info->references->references[i].id.part.lo) == -1)
				goto exception;
		}
	}
	
	if (spruce_file_util_decode_uint32 (stream, &info->flags) == -1)
		goto exception;
	
	if (spruce_file_util_decode_size_t (stream, &info->size) == -1)
		goto exception;
	
	if (spruce_file_util_decode_uint32 (stream, &info->lines) == -1)
		goto exception;
	
	/* decode user flags */
	if (spruce_file_util_decode_uint32 (stream, &count) == -1)
		goto exception;
	for (i = 0; i < count; i++) {
		char *name;
		
		if (spruce_file_util_decode_string (stream, &name) == -1)
			goto exception;
		
		spruce_flag_set (&info->user_flags, name, TRUE);
		g_free (name);
	}
	
	/* decode user tags */
	if (spruce_file_util_decode_uint32 (stream, &count) == -1)
		goto exception;
	for (i = 0; i < count; i++) {
		char *name, *value;
		
		if (spruce_file_util_decode_string (stream, &name) == -1)
			goto exception;
		if (spruce_file_util_decode_string (stream, &value) == -1)
			goto exception;
		
		if (name == NULL || value == NULL)
			continue;
		
		spruce_tag_set (&info->user_tags, name, value);
		g_free (value);
		g_free (name);
	}
	
	return info;
	
 exception:
	
	spruce_folder_summary_info_unref (summary, info);
	
	return NULL;
}


static int
summary_load (SpruceFolderSummary *summary)
{
	return -1;
}


/**
 * spruce_folder_summary_load:
 * @summary: a #SpruceFolderSummary
 *
 * Loads the summary from disk.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_summary_load (SpruceFolderSummary *summary)
{
	GMimeStream *stream, *buffered;
	SpruceMessageInfo *info;
	guint32 i = 0;
	int ret, fd;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), -1);
	g_return_val_if_fail (summary->priv->filename != NULL, -1);
	
	if (summary->loaded)
		return 0;
	
	/* we first try to load the summary file... */
	if ((fd = open (summary->priv->filename, O_RDONLY)) == -1)
		goto reload;
	
	stream = g_mime_stream_fs_new (fd);
	buffered = g_mime_stream_buffer_new (stream, GMIME_STREAM_BUFFER_BLOCK_READ);
	g_object_unref (stream);
	
	ret = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->header_load (summary, buffered);
	
	while (ret != -1 && i < summary->count) {
		if ((info = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_load (summary, buffered))) {
			g_ptr_array_add (summary->messages, info);
			g_hash_table_insert (summary->messages_hash, info->uid, info);
		} else
			ret = -1;
		
		i++;
	}
	
	g_object_unref (buffered);
	
	if (ret == -1) {
	reload:
		/* loading the summary file failed, time to do it the hard way */
		spruce_folder_summary_clear (summary);
		ret = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->summary_load (summary);
	}
	
	if (ret == 0)
		summary->loaded = TRUE;
	
	return ret;
}


static int
message_info_save (SpruceFolderSummary *summary, GMimeStream *stream, SpruceMessageInfo *info)
{
	guint32 i, count = 0;
	SpruceFlag *flag;
	SpruceTag *tag;
	
	if (spruce_file_util_encode_string (stream, info->sender) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->from) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->reply_to) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->to) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->cc) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->bcc) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->subject) == -1)
		return -1;
	
	if (spruce_file_util_encode_time_t (stream, info->date_sent) == -1)
		return -1;
	
	if (spruce_file_util_encode_time_t (stream, info->date_received) == -1)
		return -1;
	
	if (spruce_file_util_encode_string (stream, info->uid) == -1)
		return -1;
	
	/* encode Message-Id */
	if (spruce_file_util_encode_uint32 (stream, info->message_id.id.part.hi) == -1)
		return -1;
	if (spruce_file_util_encode_uint32 (stream, info->message_id.id.part.lo) == -1)
		return -1;
	
	/* encode References */
	if (info->references)
		count = info->references->count;
	if (spruce_file_util_encode_uint32 (stream, count) == -1)
		return -1;
	for (i = 0; i < count; i++) {
		if (spruce_file_util_encode_uint32 (stream, info->references->references[i].id.part.hi) == -1)
			return -1;
		if (spruce_file_util_encode_uint32 (stream, info->references->references[i].id.part.lo) == -1)
			return -1;
	}
	
	if (spruce_file_util_encode_uint32 (stream, info->flags) == -1)
		return -1;
	
	if (spruce_file_util_encode_size_t (stream, info->size) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, info->lines) == -1)
		return -1;
	
	/* encode user flags */
	count = spruce_flag_list_size (&info->user_flags);
	if (spruce_file_util_encode_uint32 (stream, count) == -1)
		return -1;
	flag = info->user_flags;
	while (flag != NULL) {
		if (spruce_file_util_encode_string (stream, flag->name) == -1)
			return -1;
		flag = flag->next;
	}
	
	/* encode user tags */
	count = spruce_tag_list_size (&info->user_tags);
	if (spruce_file_util_encode_uint32 (stream, count) == -1)
		return -1;
	tag = info->user_tags;
	while (tag != NULL) {
		if (spruce_file_util_encode_string (stream, tag->name) == -1)
			return -1;
		if (spruce_file_util_encode_string (stream, tag->value) == -1)
			return -1;
		tag = tag->next;
	}
	
	return 0;
}


static int
summary_save (SpruceFolderSummary *summary)
{
	GMimeStream *stream, *buffered;
	SpruceMessageInfo *info;
	int ret, i, fd;
	
	if ((fd = open (summary->priv->filename, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
		return -1;
	
	stream = g_mime_stream_fs_new (fd);
	buffered = g_mime_stream_buffer_new (stream, GMIME_STREAM_BUFFER_BLOCK_WRITE);
	g_object_unref (stream);
	
	ret = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->header_save (summary, buffered);
	
	for (i = 0; i < summary->messages->len && ret != -1; i++) {
		info = summary->messages->pdata[i];
		ret = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_save (summary, buffered, info);
	}
	
	if (ret != -1)
		ret = g_mime_stream_flush (buffered);
	
	g_object_unref (buffered);
	
	return ret;
}


/**
 * spruce_folder_summary_save:
 * @summary: a #SpruceFolderSummary
 *
 * Saves the summary to disk if it is loaded and dirty.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_summary_save (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), -1);
	g_return_val_if_fail (summary->priv->filename != NULL, -1);
	
	if (!summary->loaded || !summary->dirty)
		return 0;
	
	if (SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->summary_save (summary) == -1)
		return -1;
	
	summary->dirty = FALSE;
	
	return 0;
}


static int
summary_unload (SpruceFolderSummary *summary)
{
	spruce_folder_summary_clear (summary);
	
	return 0;
}


/**
 * spruce_folder_summary_unload:
 * @summary: a #SpruceFolderSummary
 *
 * Unloads the summary from memory.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_summary_unload (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), -1);
	
	if (!summary->loaded)
		return 0;
	
	if (SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->summary_unload (summary) == -1)
		return -1;
	
	summary->loaded = FALSE;
	
	return 0;
}


/**
 * spruce_folder_summary_reload:
 * @summary: a #SpruceFolderSummary
 *
 * Reloads the summary, forcing a regeneration.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_summary_reload (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), -1);
	
	if (spruce_folder_summary_unload (summary) == -1)
		return -1;
	
	if (unlink (summary->priv->filename) == -1)
		return -1;
	
	return spruce_folder_summary_load (summary);
}


/**
 * spruce_folder_summary_touch:
 * @summary: a #SpruceFolderSummary
 *
 * Sets the dirty bit on a loaded summary.
 **/
void
spruce_folder_summary_touch (SpruceFolderSummary *summary)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	
	if (summary->loaded)
		summary->dirty = TRUE;
}


static void
summary_add (SpruceFolderSummary *summary, SpruceMessageInfo *info)
{
	SpruceMessageInfo *mi;
	
	if (info->uid && (mi = g_hash_table_lookup (summary->messages_hash, info->uid))) {
		if (mi == info) {
			g_warning ("trying to re-add a message-info");
			return;
		}
		
		g_warning ("trying to add a new message-info with clashing uid");
		g_free (info->uid);
		info->uid = NULL;
	}
	
	if (info->uid == NULL)
		info->uid = spruce_folder_summary_uid_string (summary);
	
	g_hash_table_insert (summary->messages_hash, info->uid, info);
	g_ptr_array_add (summary->messages, info);
}


/**
 * spruce_folder_summary_add:
 * @summary: a #SpruceFolderSummary
 * @info: a #SpruceMessageInfo
 *
 * Adds a #SpruceMessageInfo to the summary.
 **/
void
spruce_folder_summary_add (SpruceFolderSummary *summary, SpruceMessageInfo *info)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (info != NULL);
	
	SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->add (summary, info);
	
	summary->dirty = TRUE;
}


static SpruceMessageInfo *
message_info_new (SpruceFolderSummary *summary)
{
	SpruceMessageInfo *info;
	
	info = g_slice_alloc0 (summary->message_info_size);
	info->ref_count = 1;
	
	return info;
}


/**
 * spruce_folder_summary_info_new:
 * @summary: a #SpruceFolderSummary
 *
 * Creates a new #SpruceMessageInfo for the specified summary.
 *
 * Returns: a new #SpruceMessageInfo.
 **/
SpruceMessageInfo *
spruce_folder_summary_info_new (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), NULL);
	
	return SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_new (summary);
}


static SpruceSummaryReferences *
decode_references (const char *string)
{
	SpruceSummaryReferences *references;
	GMimeReferences *refs, *r;
	unsigned char md5sum[16];
	GChecksum *checksum;
	guint32 i, n = 0;
	size_t len = 16;
	
	if (!(r = refs = g_mime_references_decode (string)))
		return NULL;
	
	while (r != NULL) {
		r = r->next;
		n++;
	}
	
	references = g_malloc (sizeof (SpruceSummaryReferences) + (sizeof (SpruceSummaryMessageID) * (n - 1)));
	references->count = n;
	
	checksum = g_checksum_new (G_CHECKSUM_MD5);
	
	for (i = 0, r = refs; i < n; i++, r = r->next) {
		g_checksum_update (checksum, (unsigned char *) r->msgid, strlen (r->msgid));
		g_checksum_get_digest (checksum, md5sum, &len);
		g_checksum_reset (checksum);
		len = 16;
		
		memcpy (references->references[i].id.hash, md5sum, sizeof (references->references[i].id.hash));
	}
	
	g_mime_references_clear (&refs);
	g_checksum_free (checksum);
	
	return references;
}

static SpruceMessageInfo *
message_info_new_from_message (SpruceFolderSummary *summary, GMimeMessage *message)
{
	InternetAddressList *ia;
	unsigned char md5sum[16];
	SpruceMessageInfo *info;
	GChecksum *checksum;
	const char *string;
	GMimeStream *null;
	size_t len = 16;
	
	info = SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_new (summary);
	
	string = g_mime_object_get_header ((GMimeObject *) message, "Sender");
	info->sender = g_strdup (string);
	
	string = g_mime_message_get_sender (message);
	info->from = g_strdup (string);
	
	string = g_mime_message_get_reply_to (message);
	info->reply_to = g_strdup (string);
	
	ia = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_TO);
	info->to = internet_address_list_to_string (ia, FALSE);
	
	ia = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_CC);
	info->cc = internet_address_list_to_string (ia, FALSE);
	
	ia = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_CC);
	info->bcc = internet_address_list_to_string (ia, FALSE);
	
	string = g_mime_message_get_subject (message);
	info->subject = g_strdup (string);
	
	g_mime_message_get_date (message, &info->date_sent, NULL);
	
	info->date_received = time (NULL);
	
	info->uid = NULL;
	
	if ((string = g_mime_message_get_message_id (message))) {
		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, string, strlen (string));
		g_checksum_get_digest (checksum, md5sum, &len);
		g_checksum_free (checksum);
		
		memcpy (info->message_id.id.hash, md5sum, sizeof (info->message_id.id.hash));
	}
	
	if (!(string = g_mime_object_get_header ((GMimeObject *) message, "References")))
		string = g_mime_object_get_header ((GMimeObject *) message, "In-Reply-To");
	
	if (string != NULL)
		info->references = decode_references (string);
	
	info->flags = 0;
	if (GMIME_IS_MULTIPART (message->mime_part)) {
		info->flags |= SPRUCE_MESSAGE_MULTIPART;
		if (GMIME_IS_MULTIPART_SIGNED (message->mime_part))
			info->flags |= SPRUCE_MESSAGE_SIGNED;
		else if (GMIME_IS_MULTIPART_ENCRYPTED (message->mime_part))
			info->flags |= SPRUCE_MESSAGE_ENCRYPTED;
	}
	
	null = g_mime_stream_null_new ();
	g_mime_object_write_to_stream ((GMimeObject *) message, null);
	info->size = ((GMimeStreamNull *) null)->written;
	g_object_unref (null);
	
	return info;
}


/**
 * spruce_folder_summary_info_new_from_message:
 * @summary: a #SpruceFolderSummary
 * @message: a #GMimeMessage
 *
 * Creates a new #SpruceMessageInfo for the specified summary and
 * populates it with info it can get from the message.
 *
 * Returns: a new #SpruceMessageInfo.
 **/
SpruceMessageInfo *
spruce_folder_summary_info_new_from_message (SpruceFolderSummary *summary, GMimeMessage *message)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), NULL);
	g_return_val_if_fail (GMIME_IS_MESSAGE (message), NULL);
	
	return SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_new_from_message (summary, message);
}


/**
 * spruce_folder_summary_info_ref:
 * @summary: a #SpruceFolderSummary
 * @info: a #SpruceMessageInfo
 *
 * Refs a #SpruceMessageInfo.
 **/
void
spruce_folder_summary_info_ref (SpruceFolderSummary *summary, SpruceMessageInfo *info)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (info != NULL);
	
	info->ref_count++;
}


static void
message_info_free (SpruceFolderSummary *summary, SpruceMessageInfo *info)
{
	g_free (info->sender);
	g_free (info->from);
	g_free (info->reply_to);
	g_free (info->to);
	g_free (info->cc);
	g_free (info->bcc);
	g_free (info->subject);
	g_free (info->uid);
	g_free (info->references);
	
	g_slice_free1 (summary->message_info_size, info);
}


/**
 * spruce_folder_summary_info_unref:
 * @summary: a #SpruceFolderSummary
 * @info: a #SpruceMessageInfo
 *
 * Unrefs a #SpruceMessageInfo.
 **/
void
spruce_folder_summary_info_unref (SpruceFolderSummary *summary, SpruceMessageInfo *info)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (info != NULL);
	
	info->ref_count--;
	if (info->ref_count <= 0)
		SPRUCE_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_free (summary, info);
}


/**
 * spruce_folder_summary_remove:
 * @summary: a #SpruceFolderSummary
 * @info: a #SpruceMessageInfo
 *
 * Removes a #SpruceMessageInfo from the summary.
 **/
void
spruce_folder_summary_remove (SpruceFolderSummary *summary, SpruceMessageInfo *info)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->uid != NULL);
	
	g_hash_table_remove (summary->messages_hash, info->uid);
	g_ptr_array_remove (summary->messages, info);
	summary->dirty = TRUE;
	
	spruce_folder_summary_info_unref (summary, info);
}


/**
 * spruce_folder_summary_remove_uid:
 * @summary: a #SpruceFolderSummary
 * @uid: a uid
 *
 * Removes the #SpruceMessageInfo with the specified uid from the
 * summary.
 **/
void
spruce_folder_summary_remove_uid (SpruceFolderSummary *summary, const char *uid)
{
	SpruceMessageInfo *info;
	
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (uid != NULL);
	
	if (!(info = g_hash_table_lookup (summary->messages_hash, uid)))
		return;
	
	spruce_folder_summary_remove (summary, info);
}


/**
 * spruce_folder_summary_remove_index:
 * @summary: a #SpruceFolderSummary
 * @index: array index
 *
 * Removes the #SpruceMessageInfo with the specified index from the
 * summary.
 **/
void
spruce_folder_summary_remove_index (SpruceFolderSummary *summary, int index)
{
	SpruceMessageInfo *info;
	
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (index >= 0 && index < summary->messages->len);
	
	info = summary->messages->pdata[index];
	g_hash_table_remove (summary->messages_hash, info->uid);
	g_ptr_array_remove_index (summary->messages, index);
	summary->dirty = TRUE;
	
	spruce_folder_summary_info_unref (summary, info);
}


/**
 * spruce_folder_summary_clear:
 * @summary: a #SpruceFolderSummary
 *
 * Clears the summary and sets the dirty bit.
 **/
void
spruce_folder_summary_clear (SpruceFolderSummary *summary)
{
	SpruceMessageInfo *info;
	int i;
	
	g_return_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary));
	
	for (i = 0; i < summary->messages->len; i++) {
		info = summary->messages->pdata[i];
		g_hash_table_remove (summary->messages_hash, info->uid);
		
		spruce_folder_summary_info_unref (summary, info);
	}
	
	g_ptr_array_set_size (summary->messages, 0);
	
	summary->dirty = TRUE;
}


/**
 * spruce_folder_summary_count:
 * @summary: a #SpruceFolderSummary
 *
 * Gets the number of entries in the summary.
 *
 * Returns: the number of entries in the summary.
 **/
int
spruce_folder_summary_count (SpruceFolderSummary *summary)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), 0);
	
	return summary->messages->len;
}


/**
 * spruce_folder_summary_uid:
 * @summary: a #SpruceFolderSummary
 * @uid: a uid
 *
 * Gets the #SpruceMessageInfo with the specified uid.
 *
 * Returns: a #SpruceMessageInfo on success or %NULL on fail.
 **/
SpruceMessageInfo *
spruce_folder_summary_uid (SpruceFolderSummary *summary, const char *uid)
{
	SpruceMessageInfo *info;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	
	if ((info = g_hash_table_lookup (summary->messages_hash, uid)))
		spruce_folder_summary_info_ref (summary, info);
	
	return info;
}


/**
 * spruce_folder_summary_index:
 * @summary: a #SpruceFolderSummary
 * @index: array index
 *
 * Gets the #SpruceMessageInfo at the specified index.
 *
 * Returns: a #SpruceMessageInfo on success or %NULL on fail.
 **/
SpruceMessageInfo *
spruce_folder_summary_index (SpruceFolderSummary *summary, int index)
{
	SpruceMessageInfo *info;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SUMMARY (summary), NULL);
	g_return_val_if_fail (index >= 0 && index < summary->messages->len, NULL);
	
	info = summary->messages->pdata[index];
	spruce_folder_summary_info_ref (summary, info);
	
	return info;
}


struct {
	char *name;
	guint32 flag;
} system_flags[] = {
	{ "Answered",  SPRUCE_MESSAGE_ANSWERED  },
	{ "Deleted",   SPRUCE_MESSAGE_DELETED   },
	{ "Draft",     SPRUCE_MESSAGE_DRAFT     },
	{ "Flagged",   SPRUCE_MESSAGE_FLAGGED   },
	{ "Forwarded", SPRUCE_MESSAGE_FORWARDED },
	{ "Recent",    SPRUCE_MESSAGE_RECENT    },
	{ "Seen",      SPRUCE_MESSAGE_SEEN      },
};


/**
 * spruce_system_flag:
 * @name: a flag name
 *
 * Looks up the bit flag for the specified flag name.
 *
 * Returns: a bit flag on success or %0 on fail.
 **/
guint32
spruce_system_flag (const char *name)
{
	guint i;
	
	for (i = 0; i < G_N_ELEMENTS (system_flags); i++) {
		if (!g_ascii_strcasecmp (name, system_flags[i].name))
			return system_flags[i].flag;
	}
	
	return 0;
}


/**
 * spruce_system_flag_is_set:
 * @flags: a set of bit flags
 * @name: a flag name
 *
 * Checks if the bit flag specified by @name is set on @flags.
 *
 * Returns: %TRUE if it is set or %FALSE otherwise.
 **/
gboolean
spruce_system_flag_is_set (guint32 flags, const char *name)
{
	guint i;
	
	for (i = 0; i < G_N_ELEMENTS (system_flags); i++) {
		if (!g_ascii_strcasecmp (name, system_flags[i].name))
			return flags & system_flags[i].flag;
	}
	
	return FALSE;
}


/**
 * spruce_flag_get:
 * @list: a list of user flags
 * @name: a flag name
 *
 * Gets the value of the specified user flag.
 *
 * Returns: %TRUE if it is set or %FALSE otherwise.
 **/
gboolean
spruce_flag_get (SpruceFlag **list, const char *name)
{
	SpruceFlag *flag;
	
	flag = *list;
	while (flag != NULL) {
		if (!strcmp (flag->name, name))
			return TRUE;
		flag = flag->next;
	}
	
	return FALSE;
}


/**
 * spruce_flag_set:
 * @list: a list of user flags
 * @name: a flag name
 * @state: the desired flag state
 *
 * Sets the state of the specified user flag.
 *
 * Returns: %TRUE if the state has changed or %FALSE otherwise.
 **/
gboolean
spruce_flag_set (SpruceFlag **list, const char *name, gboolean state)
{
	SpruceFlag *flag, *node;
	
	node = (SpruceFlag *) list;
	while (node->next != NULL) {
		flag = node->next;
		if (!strcmp (flag->name, name)) {
			if (!state) {
				node->next = flag->next;
				g_free (flag);
			}
			
			return !state;
		}
		
		node = flag;
	}
	
	if (state) {
		node->next = flag = g_malloc (sizeof (SpruceFlag) + strlen (name));
		strcpy (flag->name, name);
		flag->next = NULL;
	}
	
	return state;
}


/**
 * spruce_flag_list_copy:
 * @to: a list of user flags
 * @from: a list of user flags
 *
 * Copies @from to @to.
 *
 * Returns: %TRUE if @to has changed or %FALSE otherwise.
 **/
gboolean
spruce_flag_list_copy (SpruceFlag **to, SpruceFlag **from)
{
	SpruceFlag *flag, *node;
	int changed = FALSE;
	
	if (*to == NULL && from == NULL)
		return FALSE;
	
	/* Remove any now-missing flags */
	node = (SpruceFlag *) to;
	while (node->next) {
		flag = node->next;
		if (!spruce_flag_get (from, flag->name)) {
			node->next = flag->next;
			g_free (flag);
			changed = TRUE;
		} else {
			node = flag;
		}
	}
	
	/* Add any new flags */
	flag = *from;
	while (flag) {
		changed = changed || spruce_flag_set (to, flag->name, TRUE);
		flag = flag->next;
	}
	
	return changed;
}


/**
 * spruce_flag_list_size:
 * @list: a user flag list
 *
 * Gets the number of user flags in @list.
 *
 * Returns: the number of user flags in @list.
 **/
int
spruce_flag_list_size (SpruceFlag **list)
{
	SpruceFlag *flag;
	int count = 0;
	
	flag = *list;
	while (flag != NULL) {
		flag = flag->next;
		count++;
	}
	
	return count;
}


/**
 * spruce_flag_list_free:
 * @list: a user flag list
 *
 * Frees the list of user flags.
 **/
void
spruce_flag_list_free (SpruceFlag **list)
{
	SpruceFlag *flag, *next;
	
	flag = *list;
	while (flag != NULL) {
		next = flag->next;
		g_free (flag);
		flag = next;
	}
	
	*list = NULL;
}


/**
 * spruce_tag_get:
 * @list: a list of user tags
 * @name: a tag name
 *
 * Gets the value of the specified user tag.
 *
 * Returns: the string value of the specified tag or %NULL if not
 * found.
 **/
const char *
spruce_tag_get (SpruceTag **list, const char *name)
{
	SpruceTag *tag;
	
	tag = *list;
	while (tag != NULL) {
		if (!strcmp (tag->name, name))
			return tag->value;
		tag = tag->next;
	}
	
	return NULL;
}


/**
 * spruce_tag_set:
 * @list: a list of user tags
 * @name: a tag name
 * @value: the desired tag value
 *
 * Sets the value of the specified user tag.
 *
 * Returns: %TRUE if the value has changed or %FALSE otherwise.
 **/
gboolean
spruce_tag_set (SpruceTag **list, const char *name, const char *value)
{
	SpruceTag *tag, *node;
	
	node = (SpruceTag *) list;
	while (node->next != NULL) {
		tag = node->next;
		if (!strcmp (tag->name, name)) {
			if (!value) {
				node->next = tag->next;
				g_free (tag->value);
				g_free (tag);
				return TRUE;
			} else if (tag->value && strcmp (tag->value, value) != 0) {
				g_free (tag->value);
				tag->value = g_strdup (value);
				return TRUE;
			}
			
			return FALSE;
		}
		
		node = tag;
	}
	
	if (value) {
		node->next = tag = g_malloc (sizeof (SpruceTag) + strlen (name));
		tag->value = g_strdup (value);
		strcpy (tag->name, name);
		tag->next = NULL;
	}
	
	return value != NULL;
}


static void
rem_tag (char *name, char *value, SpruceTag **list)
{
	spruce_tag_set (list, name, NULL);
}


/**
 * spruce_tag_list_copy:
 * @to: a list of user tags
 * @from: a list of user tags
 *
 * Copies @from to @to.
 *
 * Returns: %TRUE if @to has changed or %FALSE otherwise.
 **/
gboolean
spruce_tag_list_copy (SpruceTag **to, SpruceTag **from)
{
	int changed = FALSE;
	GHashTable *left;
	SpruceTag *tag;
	
	if (*to == NULL && from == NULL)
		return FALSE;
	
	left = g_hash_table_new (g_str_hash, g_str_equal);
	
	tag = *to;
	while (tag) {
		g_hash_table_insert (left, tag->name, tag);
		tag = tag->next;
	}
	
	tag = *from;
	while (tag) {
		changed |= spruce_tag_set (to, tag->name, tag->value);
		g_hash_table_remove (left, tag->name);
		tag = tag->next;
	}
	
	if (g_hash_table_size (left) > 0) {
		g_hash_table_foreach (left, (GHFunc) rem_tag, to);
		changed = TRUE;
	}
	
	g_hash_table_destroy (left);
	
	return changed;
}


/**
 * spruce_tag_list_size:
 * @list: a user tag list
 *
 * Gets the number of user tags in @list.
 *
 * Returns: the number of user tags in @list.
 **/
int
spruce_tag_list_size (SpruceTag **list)
{
	SpruceTag *tag;
	int count = 0;
	
	tag = *list;
	while (tag != NULL) {
		if (tag->value)
			count++;
		tag = tag->next;
	}
	
	return count;
}


/**
 * spruce_tag_list_free:
 * @list: a user tag list
 *
 * Frees the list of user tags.
 **/
void
spruce_tag_list_free (SpruceTag **list)
{
	SpruceTag *tag, *next;
	
	tag = *list;
	while (tag != NULL) {
		next = tag->next;
		g_free (tag->value);
		g_free (tag);
		tag = next;
	}
	
	*list = NULL;
}
