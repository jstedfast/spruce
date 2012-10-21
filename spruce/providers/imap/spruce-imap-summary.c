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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <utime.h>
#include <fcntl.h>
#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <gmime/gmime-utils.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-cache.h>
#include <spruce/spruce-file-utils.h>

#include "spruce-imap-store.h"
#include "spruce-imap-engine.h"
#include "spruce-imap-folder.h"
#include "spruce-imap-stream.h"
#include "spruce-imap-command.h"
#include "spruce-imap-utils.h"

#include "spruce-imap-summary.h"


#define IMAP_SUMMARY_VERSION  1

#define IMAP_SAVE_INCREMENT 1024

static void spruce_imap_summary_class_init (SpruceIMAPSummaryClass *klass);
static void spruce_imap_summary_init (SpruceIMAPSummary *summary, SpruceIMAPSummaryClass *klass);
static void spruce_imap_summary_finalize (GObject *object);

static int imap_header_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int imap_header_save (SpruceFolderSummary *summary, GMimeStream *stream);
static int imap_summary_load (SpruceFolderSummary *summary);
static int imap_summary_save (SpruceFolderSummary *summary);
static SpruceMessageInfo *imap_message_info_new (SpruceFolderSummary *summary);
static SpruceMessageInfo *imap_message_info_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int imap_message_info_save (SpruceFolderSummary *summary, GMimeStream *stream, SpruceMessageInfo *info);


static SpruceFolderSummaryClass *parent_class = NULL;


GType
spruce_imap_summary_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceIMAPSummaryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_imap_summary_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceIMAPSummary),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_imap_summary_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER_SUMMARY, "SpruceIMAPSummary", &info, 0);
	}
	
	return type;
}


static void
spruce_imap_summary_class_init (SpruceIMAPSummaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderSummaryClass *summary_class = SPRUCE_FOLDER_SUMMARY_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER_SUMMARY);
	
	object_class->finalize = spruce_imap_summary_finalize;
	
	summary_class->header_load = imap_header_load;
	summary_class->header_save = imap_header_save;
	summary_class->summary_load = imap_summary_load;
	summary_class->summary_save = imap_summary_save;
	summary_class->message_info_new = imap_message_info_new;
	summary_class->message_info_load = imap_message_info_load;
	summary_class->message_info_save = imap_message_info_save;
}

static void
spruce_imap_summary_init (SpruceIMAPSummary *summary, SpruceIMAPSummaryClass *klass)
{
	SpruceFolderSummary *folder_summary = (SpruceFolderSummary *) summary;
	
	folder_summary->version += IMAP_SUMMARY_VERSION;
	folder_summary->flags = SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED |
		SPRUCE_MESSAGE_DRAFT | SPRUCE_MESSAGE_FLAGGED | SPRUCE_MESSAGE_SEEN;
	
	folder_summary->message_info_size = sizeof (SpruceIMAPMessageInfo);
	
	summary->uidvalidity_changed = FALSE;
	summary->update_flags = TRUE;
}

static void
spruce_imap_summary_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


SpruceFolderSummary *
spruce_imap_summary_new (SpruceFolder *folder)
{
	SpruceFolderSummary *summary;
	
	summary = g_object_new (SPRUCE_TYPE_IMAP_SUMMARY, NULL);
	((SpruceIMAPSummary *) summary)->folder = folder;
	
	return summary;
}

static int
imap_header_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	
	if (SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->header_load (summary, stream) == -1)
		return -1;
	
	if (spruce_file_util_decode_uint32 (stream, &imap_summary->uidvalidity) == -1)
		return -1;
	
	return 0;
}

static int
imap_header_save (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	
	if (SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->header_save (summary, stream) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, imap_summary->uidvalidity) == -1)
		return -1;
	
	return 0;
}

static int
envelope_decode_address (SpruceIMAPEngine *engine, GString *addrs, GError **err)
{
	char *addr, *name = NULL, *user = NULL;
	unsigned char *literal = NULL;
	spruce_imap_token_t token;
	const char *domain = NULL;
	InternetAddress *ia;
	int part = 0;
	size_t n;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token == SPRUCE_IMAP_TOKEN_NIL) {
		return 0;
	} else if (token.token != '(') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	if (addrs->len > 0)
		g_string_append (addrs, ", ");
	
	do {
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
		
		literal = NULL;
		switch (token.token) {
		case SPRUCE_IMAP_TOKEN_NIL:
			break;
		case SPRUCE_IMAP_TOKEN_ATOM:
		case SPRUCE_IMAP_TOKEN_QSTRING:
			switch (part) {
			case 0:
				name = g_mime_utils_header_decode_phrase (token.v.qstring);
				break;
			case 2:
				user = g_strdup (token.v.qstring);
				break;
			case 3:
				domain = token.v.qstring;
				break;
			}
			break;
		case SPRUCE_IMAP_TOKEN_LITERAL:
			if (spruce_imap_engine_literal (engine, &literal, &n, err) == -1)
				goto exception;
			
			switch (part) {
			case 0:
				name = g_mime_utils_header_decode_phrase ((char *) literal);
				g_free (literal);
				break;
			case 2:
				user = (char *) literal;
				break;
			case 3:
				domain = (char *) literal;
				break;
			}
			
			break;
		default:
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		part++;
	} while (part < 4);
	
	addr = g_strdup_printf ("%s@%s", user, domain);
	g_free (literal);
	g_free (user);
	
	ia = internet_address_mailbox_new (name, addr);
	g_free (name);
	g_free (addr);
	
	addr = internet_address_to_string (ia, FALSE);
	g_object_unref (ia);
	
	g_string_append (addrs, addr);
	g_free (addr);
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token != ')') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	return 0;
	
 exception:
	
	g_free (name);
	g_free (user);
	
	return -1;
}

static int
envelope_decode_addresses (SpruceIMAPEngine *engine, char **addrlist, GError **err)
{
	spruce_imap_token_t token;
	GString *addrs;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token == SPRUCE_IMAP_TOKEN_NIL) {
		*addrlist = NULL;
		return 0;
	} else if (token.token != '(') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	addrs = g_string_new ("");
	
	do {
		if (spruce_imap_engine_next_token (engine, &token, err) == -1) {
			g_string_free (addrs, TRUE);
			return -1;
		}
		
		if (token.token == '(') {
			spruce_imap_stream_unget_token (engine->istream, &token);
			
			if (envelope_decode_address (engine, addrs, err) == -1) {
				g_string_free (addrs, TRUE);
				return -1;
			}
		} else if (token.token == ')') {
			break;
		} else {
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			return -1;
		}
	} while (1);
	
	*addrlist = addrs->str;
	g_string_free (addrs, FALSE);
	
	return 0;
}

static int
envelope_decode_date (SpruceIMAPEngine *engine, time_t *date, GError **err)
{
	unsigned char *literal = NULL;
	spruce_imap_token_t token;
	const char *nstring;
	size_t n;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	switch (token.token) {
	case SPRUCE_IMAP_TOKEN_NIL:
		*date = (time_t) -1;
		return 0;
	case SPRUCE_IMAP_TOKEN_ATOM:
		nstring = token.v.atom;
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
		nstring = token.v.qstring;
		break;
	case SPRUCE_IMAP_TOKEN_LITERAL:
		if (spruce_imap_engine_literal (engine, &literal, &n, err) == -1)
			return -1;
		
		nstring = (char *) literal;
		break;
	default:
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	*date = g_mime_utils_header_decode_date (nstring, NULL);
	
	g_free (literal);
	
	return 0;
}

static int
envelope_decode_nstring (SpruceIMAPEngine *engine, char **nstring, gboolean rfc2047, GError **err)
{
	spruce_imap_token_t token;
	unsigned char *literal;
	size_t n;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	switch (token.token) {
	case SPRUCE_IMAP_TOKEN_NIL:
		*nstring = NULL;
		break;
	case SPRUCE_IMAP_TOKEN_ATOM:
		if (rfc2047)
			*nstring = g_mime_utils_header_decode_text (token.v.atom);
		else
			*nstring = g_strdup (token.v.atom);
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
		if (rfc2047)
			*nstring = g_mime_utils_header_decode_text (token.v.qstring);
		else
			*nstring = g_strdup (token.v.qstring);
		break;
	case SPRUCE_IMAP_TOKEN_LITERAL:
		if (spruce_imap_engine_literal (engine, &literal, &n, err) == -1)
			return -1;
		
		if (rfc2047) {
			*nstring = g_mime_utils_header_decode_text ((char *) literal);
			g_free (literal);
		} else
			*nstring = (char *) literal;
		
		break;
	default:
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	return 0;
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
		g_checksum_update (checksum, r->msgid, strlen (r->msgid));
		g_checksum_get_digest (checksum, md5sum, &len);
		g_checksum_reset (checksum);
		len = 16;
		
		memcpy (references->references[i].id.hash, md5sum, sizeof (references->references[i].id.hash));
	}
	
	g_mime_references_clear (&refs);
	g_checksum_free (checksum);
	
	return references;
}

static int
decode_envelope (SpruceIMAPEngine *engine, SpruceMessageInfo *info, spruce_imap_token_t *token, GError **err)
{
	unsigned char md5sum[16];
	char *nstring, *msgid;
	GChecksum *checksum;
	size_t len = 16;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	if (token->token != '(') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		return -1;
	}
	
	if (envelope_decode_date (engine, &info->date_sent, err) == -1)
		goto exception;
	
	if (envelope_decode_nstring (engine, &info->subject, TRUE, err) == -1)
		goto exception;
	
	if (envelope_decode_addresses (engine, &info->from, err) == -1)
		goto exception;
	
	if (envelope_decode_addresses (engine, &info->sender, err) == -1)
		goto exception;
	
	if (envelope_decode_addresses (engine, &info->reply_to, err) == -1)
		goto exception;
	
	if (envelope_decode_addresses (engine, &info->to, err) == -1)
		goto exception;
	
	if (envelope_decode_addresses (engine, &info->cc, err) == -1)
		goto exception;
	
	if (envelope_decode_addresses (engine, &info->bcc, err) == -1)
		goto exception;
	
	if (envelope_decode_nstring (engine, &nstring, FALSE, err) == -1)
		goto exception;
	
	if (nstring != NULL) {
		info->references = decode_references (nstring);
		g_free (nstring);
	}
	
	if (envelope_decode_nstring (engine, &nstring, FALSE, err) == -1)
		goto exception;
	
	if (nstring != NULL) {
		if ((msgid = g_mime_utils_decode_message_id (nstring))) {
			checksum = g_checksum_new (G_CHECKSUM_MD5);
			g_checksum_update (checksum, msgid, strlen (msgid));
			g_checksum_get_digest (checksum, md5sum, &len);
			g_checksum_free (checksum);
			
			memcpy (info->message_id.id.hash, md5sum, sizeof (info->message_id.id.hash));
			g_free (msgid);
		}
		g_free (nstring);
	}
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	if (token->token != ')') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		goto exception;
	}
	
	return 0;
	
 exception:
	
	return -1;
}

static char *tm_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static gboolean
decode_time (const char **in, int *hour, int *min, int *sec)
{
	register const unsigned char *inptr = (const unsigned char *) *in;
	int *val, colons = 0;
	
	*hour = *min = *sec = 0;
	
	val = hour;
	for ( ; *inptr && !isspace ((int) *inptr); inptr++) {
		if (*inptr == ':') {
			colons++;
			switch (colons) {
			case 1:
				val = min;
				break;
			case 2:
				val = sec;
				break;
			default:
				return FALSE;
			}
		} else if (!isdigit ((int) *inptr))
			return FALSE;
		else
			*val = (*val * 10) + (*inptr - '0');
	}
	
	*in = (const char *) inptr;
	
	return TRUE;
}

static time_t
mktime_utc (struct tm *tm)
{
	time_t tt;
	
	tm->tm_isdst = -1;
	tt = mktime (tm);
	
#if defined (HAVE_TM_GMTOFF)
	tt += tm->tm_gmtoff;
#elif defined (HAVE_TIMEZONE)
	if (tm->tm_isdst > 0) {
#if defined (HAVE_ALTZONE)
		tt -= altzone;
#else /* !defined (HAVE_ALTZONE) */
		tt -= (timezone - 3600);
#endif
	} else
		tt -= timezone;
#else
#error Neither HAVE_TIMEZONE nor HAVE_TM_GMTOFF defined. Rerun autoheader, autoconf, etc.
#endif
	
	return tt;
}

static time_t
decode_internaldate (const char *in)
{
	const char *inptr = in;
	int hour, min, sec, n;
	struct tm tm;
	time_t date;
	char *buf;
	
	memset ((void *) &tm, 0, sizeof (struct tm));
	
	tm.tm_mday = strtoul (inptr, &buf, 10);
	if (buf == inptr || *buf != '-')
		return (time_t) -1;
	
	inptr = buf + 1;
	if (inptr[3] != '-')
		return (time_t) -1;
	
	for (n = 0; n < 12; n++) {
		if (!g_ascii_strncasecmp (inptr, tm_months[n], 3))
			break;
	}
	
	if (n >= 12)
		return (time_t) -1;
	
	tm.tm_mon = n;
	
	inptr += 4;
	
	n = strtoul (inptr, &buf, 10);
	if (buf == inptr || *buf != ' ')
		return (time_t) -1;
	
	tm.tm_year = n - 1900;
	
	inptr = buf + 1;
	if (!decode_time (&inptr, &hour, &min, &sec))
		return (time_t) -1;
	
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;
	
	n = strtol (inptr, NULL, 10);
	
	date = mktime_utc (&tm);
	
	/* date is now GMT of the time we want, but not offset by the timezone ... */
	
	/* this should convert the time to the GMT equiv time */
	date -= ((n / 100) * 60 * 60) + (n % 100) * 60;
	
	return date;
}

enum {
	IMAP_FETCH_ENVELOPE     = (1 << 0),
	IMAP_FETCH_FLAGS        = (1 << 1),
	IMAP_FETCH_INTERNALDATE = (1 << 2),
	IMAP_FETCH_RFC822SIZE   = (1 << 3),
	IMAP_FETCH_UID          = (1 << 4),
	
	IMAP_FETCH_SAVED        = (1 << 7),
};

#define IMAP_FETCH_ALL (IMAP_FETCH_ENVELOPE | IMAP_FETCH_FLAGS | IMAP_FETCH_INTERNALDATE | IMAP_FETCH_RFC822SIZE | IMAP_FETCH_UID)

struct imap_envelope_t {
	SpruceMessageInfo *info;
	guint8 changed;
};

struct imap_fetch_all_t {
	SpruceFolderChangeInfo *changes;
	SpruceFolderSummary *summary;
	GHashTable *uid_hash;
	GPtrArray *added;
	guint32 count;
	guint32 total;
	guint32 first;
	guint8 need;
	guint8 all;
};

static void
imap_fetch_all_free (struct imap_fetch_all_t *fetch)
{
	struct imap_envelope_t *envelope;
	int i;
	
	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i]))
			continue;
		
		spruce_folder_summary_info_unref (fetch->summary, envelope->info);
		g_free (envelope);
	}
	
	g_ptr_array_free (fetch->added, TRUE);
	g_hash_table_destroy (fetch->uid_hash);
	
	spruce_folder_change_info_free (fetch->changes);
	
	g_free (fetch);
}

static void
courier_imap_is_a_piece_of_shit (SpruceFolderSummary *summary, guint32 msg)
{
	SpruceIMAPSummary *imap = (SpruceIMAPSummary *) summary;
	SpruceSession *session = ((SpruceService *) ((SpruceFolder *) imap->folder)->store)->session;
	char *warning;
	
	warning = g_strdup_printf ("IMAP server did not respond with an untagged FETCH response for\n"
				   "message #%u. This is illegal according to rfc3501 (and the older\n"
				   "rfc2060). You will need to contact your Administrator(s) (or ISP)\n"
				   "and have them resolve this issue.\n\n"
				   "Hint: If your IMAP server is Courier-IMAP, it is likely that this\n"
				   "message is simply unreadable by the IMAP server and will need to\n"
				   "be given read permissions.\n", msg);
	
	spruce_session_alert_user (session, warning);
	g_free (warning);
}

/**
 * imap_fetch_all_add:
 * @fetch: FETCH ALL state
 * @complete: %TRUE if the FETCH command is complete or %FALSE otherwise
 *
 * Adds all newly acquired envelopes to the summary. Stops at the
 * first incomplete envelope.
 **/
static void
imap_fetch_all_add (struct imap_fetch_all_t *fetch, gboolean complete)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) fetch->summary;
	struct imap_envelope_t *envelope;
	SpruceFolderChangeInfo *changes;
	SpruceMessageInfo *info;
	guint32 i;
	
	changes = fetch->changes;
	
	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i])) {
			if (complete)
				courier_imap_is_a_piece_of_shit (fetch->summary, i + fetch->first);
			break;
		}
		
		if ((envelope->changed & IMAP_FETCH_ALL) != IMAP_FETCH_ALL) {
			/* server hasn't given us all the requested info for this message. */
			if (complete) {
				fprintf (stderr, "Hmmm, IMAP server didn't give us everything for message %d\n",
					 i + fetch->first);
			}
			
			break;
		}
		
		if (!(envelope->changed & IMAP_FETCH_SAVED)) {
			if ((info = spruce_folder_summary_uid (fetch->summary, envelope->info->uid)))
				continue;
			
			spruce_folder_change_info_add_uid (changes, envelope->info->uid);
			spruce_folder_summary_info_ref (fetch->summary, envelope->info);
			spruce_folder_summary_add (fetch->summary, envelope->info);
			envelope->changed |= IMAP_FETCH_SAVED;
		}
	}
	
	if (complete) {
		for (i = 0; i < fetch->added->len; i++) {
			if (!(envelope = fetch->added->pdata[i]))
				continue;
			
			spruce_folder_summary_info_unref (fetch->summary, envelope->info);
			g_free (envelope);
		}
		
		g_ptr_array_free (fetch->added, TRUE);
		g_hash_table_destroy (fetch->uid_hash);
	}
	
	if (spruce_folder_change_info_changed (changes))
		g_signal_emit_by_name (imap_summary->folder, "folder-changed", changes);
	
	if (complete) {
		spruce_folder_change_info_free (changes);
		g_free (fetch);
	} else {
		spruce_folder_summary_save (fetch->summary);
		spruce_folder_change_info_clear (changes);
	}
}

static void
imap_fetch_all_update (struct imap_fetch_all_t *fetch)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) fetch->summary;
	SpruceCache *cache = ((SpruceIMAPFolder *) imap_summary->folder)->cache;
	SpruceIMAPMessageInfo *iinfo, *new_iinfo;
	struct imap_envelope_t *envelope;
	SpruceFolderChangeInfo *changes;
	SpruceMessageInfo *info;
	guint32 flags;
	int total, i;
	
	changes = fetch->changes;
	
	total = spruce_folder_summary_count (fetch->summary);
	for (i = 0; i < total; i++) {
		info = spruce_folder_summary_index (fetch->summary, i);
		if (!(envelope = g_hash_table_lookup (fetch->uid_hash, info->uid))) {
			/* this message has been expunged from the server */
			spruce_cache_expire_key (cache, info->uid, NULL);
			spruce_folder_change_info_remove_uid (changes, info->uid);
			spruce_folder_summary_remove (fetch->summary, info);
			total--;
			i--;
		} else if (envelope->changed & IMAP_FETCH_FLAGS) {
			/* update it with the new flags */
			new_iinfo = (SpruceIMAPMessageInfo *) envelope->info;
			iinfo = (SpruceIMAPMessageInfo *) info;
			
			flags = info->flags;
			info->flags = spruce_imap_merge_flags (iinfo->server_flags, info->flags, new_iinfo->server_flags);
			iinfo->server_flags = new_iinfo->server_flags;
			if (info->flags != flags)
				spruce_folder_change_info_change_uid (changes, info->uid);
		}
		
		spruce_folder_summary_info_unref (fetch->summary, info);
	}
	
	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i])) {
			courier_imap_is_a_piece_of_shit (fetch->summary, fetch->first + i);
			continue;
		}
		
		spruce_folder_summary_info_unref (fetch->summary, envelope->info);
		g_free (envelope);
	}
	
	g_ptr_array_free (fetch->added, TRUE);
	g_hash_table_destroy (fetch->uid_hash);
	
	if (spruce_folder_change_info_changed (changes))
		g_signal_emit_by_name (imap_summary->folder, "folder-changed", changes);
	spruce_folder_change_info_free (changes);
	
	g_free (fetch);
}

static int
untagged_fetch_all (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, guint32 index, spruce_imap_token_t *token, GError **err)
{
	struct imap_fetch_all_t *fetch = ic->user_data;
	SpruceFolderSummary *summary = fetch->summary;
	struct imap_envelope_t *envelope = NULL;
	GPtrArray *added = fetch->added;
	SpruceIMAPMessageInfo *iinfo;
	SpruceMessageInfo *info;
	guint32 changed = 0;
	char uid[16];
	
	if (index < fetch->first) {
		/* This can happen if the connection to the
		 * server was dropped in a previous attempt at
		 * this FETCH (ALL) command and some other
		 * client expunged messages in the range
		 * before fetch->first in the period between
		 * our previous attempt and now. */
		size_t movelen = added->len * sizeof (void *);
		size_t extra = index - fetch->first;
		void *dest;
		
		g_assert (fetch->all);
		
		g_ptr_array_set_size (added, added->len + extra);
		dest = ((char *) added->pdata) + (extra * sizeof (void *));
		memmove (dest, added->pdata, movelen);
		fetch->total += extra;
		fetch->first = index;
	} else if (index > (added->len + (fetch->first - 1))) {
		size_t extra = index - (added->len + (fetch->first - 1));
		g_ptr_array_set_size (added, added->len + extra);
		fetch->total += extra;
	}
	
	if (!(envelope = added->pdata[index - fetch->first])) {
		info = spruce_folder_summary_info_new (summary);
		iinfo = (SpruceIMAPMessageInfo *) info;
		envelope = g_new (struct imap_envelope_t, 1);
		added->pdata[index - fetch->first] = envelope;
		envelope->info = info;
		envelope->changed = 0;
	} else {
		info = envelope->info;
		iinfo = (SpruceIMAPMessageInfo *) info;
	}
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	/* parse the FETCH response list */
	if (token->token != '(') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		return -1;
	}
	
	do {
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			goto exception;
		
		if (token->token == ')' || token->token == '\n')
			break;
		
		if (token->token != SPRUCE_IMAP_TOKEN_ATOM)
			goto unexpected;
		
		if (!strcmp (token->v.atom, "ENVELOPE")) {
			if (envelope) {
				if (decode_envelope (engine, info, token, err) == -1)
					goto exception;
				
				changed |= IMAP_FETCH_ENVELOPE;
			} else {
				SpruceMessageInfo *tmp;
				int rv;
				
				g_warning ("Hmmm, server is sending us ENVELOPE data for a message we didn't ask for (message %u)\n",
					   index);
				tmp = spruce_folder_summary_info_new (summary);
				rv = decode_envelope (engine, tmp, token, err);
				spruce_folder_summary_info_unref (summary, tmp);
				
				if (rv == -1)
					goto exception;
			}
		} else if (!strcmp (token->v.atom, "FLAGS")) {
			guint32 server_flags = 0;
			
			if (spruce_imap_parse_flags_list (engine, &server_flags, err) == -1)
				return -1;
			
			info->flags = spruce_imap_merge_flags (iinfo->server_flags, info->flags, server_flags);
			iinfo->server_flags = server_flags;
			
			changed |= IMAP_FETCH_FLAGS;
		} else if (!strcmp (token->v.atom, "INTERNALDATE")) {
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				goto exception;
			
			switch (token->token) {
			case SPRUCE_IMAP_TOKEN_NIL:
				info->date_received = (time_t) -1;
				break;
			case SPRUCE_IMAP_TOKEN_ATOM:
			case SPRUCE_IMAP_TOKEN_QSTRING:
				info->date_received = decode_internaldate (token->v.qstring);
				break;
			default:
				goto unexpected;
			}
			
			changed |= IMAP_FETCH_INTERNALDATE;
		} else if (!strcmp (token->v.atom, "RFC822.SIZE")) {
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				goto exception;
			
			if (token->token != SPRUCE_IMAP_TOKEN_NUMBER)
				goto unexpected;
			
			info->size = token->v.number;
			
			changed |= IMAP_FETCH_RFC822SIZE;
		} else if (!strcmp (token->v.atom, "UID")) {
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				goto exception;
			
			if (token->token != SPRUCE_IMAP_TOKEN_NUMBER || token->v.number == 0)
				goto unexpected;
			
			sprintf (uid, "%u", token->v.number);
			if (info->uid != NULL) {
				if (strcmp (info->uid, uid) != 0) {
					fprintf (stderr, "Hmmm, UID mismatch for message %u\n", index);
					g_assert_not_reached ();
				}
			} else if (envelope) {
				info->uid = g_strdup (uid);
				g_hash_table_insert (fetch->uid_hash, info->uid, envelope);
				changed |= IMAP_FETCH_UID;
			}
		} else {
			/* wtf? */
			fprintf (stderr, "huh? %s?...\n", token->v.atom);
		}
	} while (1);
	
	if (envelope) {
		envelope->changed |= changed;
		
		if ((envelope->changed & fetch->need) == fetch->need) {
			fetch->count++;
			
			/* if we're doing a FETCH ALL and fetch->count
			 * is a multiple of the IMAP_SAVE_INCREMENT,
			 * sync the newly fetched envelopes to the
			 * summary and to disk as a convenience to
			 * users on flaky networks which might drop
			 * our connection to the IMAP server at any
			 * time, thus forcing us to reconnect and lose
			 * our summary fetching state. */
			if (fetch->all && (fetch->count % IMAP_SAVE_INCREMENT) == 0)
				imap_fetch_all_add (fetch, FALSE);
			
			/*spruce_operation_progress (NULL, (fetch->count * 100.0f) / fetch->total);*/
		}
	} else if (changed & IMAP_FETCH_FLAGS) {
		spruce_folder_change_info_change_uid (fetch->changes, info->uid);
	}
	
	if (token->token != ')')
		goto unexpected;
	
	return 0;
	
 unexpected:
	
	spruce_imap_utils_set_unexpected_token_error (err, engine, token);
	
 exception:
	
	fprintf (stderr, "***ERROR: %s\n", (*err)->message);
	
	return -1;
}


/* From rfc2060, Section 6.4.5:
 * 
 * The currently defined data items that can be fetched are:
 *
 * ALL            Macro equivalent to: (FLAGS INTERNALDATE
 *                RFC822.SIZE ENVELOPE)
 **/
#define IMAP4_ALL "FLAGS INTERNALDATE RFC822.SIZE ENVELOPE"

static void
imap_fetch_all_reset (SpruceIMAPCommand *ic, struct imap_fetch_all_t *fetch)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) fetch->summary;
	struct imap_envelope_t *envelope;
	SpruceMessageInfo *info;
	guint32 seqid, iuid;
	char uid[32];
	int scount;
	int i;
	
	/* sync everything we've gotten so far to the summary */
	imap_fetch_all_add (fetch, FALSE);
	
	for (i = 0; i < fetch->added->len; i++) {
		if (!(envelope = fetch->added->pdata[i]))
			continue;
		
		spruce_folder_summary_info_unref (fetch->summary, envelope->info);
		fetch->added->pdata[i] = NULL;
		g_free (envelope);
	}
	
	scount = spruce_folder_summary_count (fetch->summary);
	seqid = scount + 1;
	
	if (seqid > fetch->first) {
		/* if we get here, then it means that we managed to
		 * collect some summary info before the connection
		 * with the imap server dropped. Update our FETCH
		 * command state to begin fetching where we left off
		 * rather than at the beginning. */
		info = spruce_folder_summary_index (fetch->summary, scount - 1);
		iuid = strtoul (info->uid, NULL, 10);
		fprintf (stderr, "last known summary id = %d, uid = %s, iuid = %u\n", scount, info->uid, iuid);
		spruce_folder_summary_info_unref (fetch->summary, info);
		sprintf (uid, "%u", iuid + 1);
		
		fetch->total = imap_summary->exists - scount;
		g_ptr_array_set_size (fetch->added, fetch->total);
		fetch->first = seqid;
		
		/* now we hack the SpruceIMAPCommand structure... */
		g_free (ic->part->buffer);
		ic->part->buffer = g_strdup_printf ("UID FETCH %s:* (ALL)\r\n", uid);
		ic->part->buflen = strlen (ic->part->buffer);
		
		fprintf (stderr, "*** RESETTING FETCH-ALL STATE. New command => %s", ic->part->buffer);
	} else {
		/* we didn't manage to fetch any new info before the
		 * connection dropped... */
	}
	
	spruce_folder_change_info_clear (fetch->changes);
	g_hash_table_remove_all (fetch->uid_hash);
}

static SpruceIMAPCommand *
imap_summary_fetch_all (SpruceFolderSummary *summary, guint32 seqid, const char *uid)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	SpruceFolder *folder = imap_summary->folder;
	struct imap_fetch_all_t *fetch;
	SpruceIMAPEngine *engine;
	SpruceIMAPCommand *ic;
	guint32 total;
	
	engine = ((SpruceIMAPStore *) folder->store)->engine;
	
	total = (imap_summary->exists - seqid) + 1;
	fetch = g_new (struct imap_fetch_all_t, 1);
	fetch->uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	fetch->changes = spruce_folder_change_info_new ();
	fetch->added = g_ptr_array_sized_new (total);
	fetch->summary = summary;
	fetch->first = seqid;
	fetch->need = IMAP_FETCH_ALL;
	fetch->total = total;
	fetch->count = 0;
	fetch->all = TRUE;
	
	ic = spruce_imap_engine_queue (engine, folder, "UID FETCH %s:* (ALL)\r\n", uid);
	
	spruce_imap_command_register_untagged (ic, "FETCH", untagged_fetch_all);
	ic->reset = (SpruceIMAPCommandReset) imap_fetch_all_reset;
	ic->user_data = fetch;
	
	return ic;
}

static SpruceIMAPCommand *
imap_summary_fetch_flags (SpruceFolderSummary *summary)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	SpruceFolder *folder = imap_summary->folder;
	struct imap_fetch_all_t *fetch;
	SpruceMessageInfo *info[2];
	SpruceIMAPEngine *engine;
	SpruceIMAPCommand *ic;
	guint32 total;
	int scount;
	
	engine = ((SpruceIMAPStore *) folder->store)->engine;
	
	scount = spruce_folder_summary_count (summary);
	g_assert (scount > 0);
	
	info[0] = spruce_folder_summary_index (summary, 0);
	if (scount > 1)
		info[1] = spruce_folder_summary_index (summary, scount - 1);
	else
		info[1] = NULL;
	
	total = imap_summary->exists < scount ? imap_summary->exists : scount;
	fetch = g_new (struct imap_fetch_all_t, 1);
	fetch->uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	fetch->changes = spruce_folder_change_info_new ();
	fetch->added = g_ptr_array_sized_new (total);
	fetch->summary = summary;
	fetch->first = 1;
	fetch->need = IMAP_FETCH_UID | IMAP_FETCH_FLAGS;
	fetch->total = total;
	fetch->count = 0;
	fetch->all = FALSE;
	
	if (info[1] != NULL) {
		ic = spruce_imap_engine_queue (engine, folder, "UID FETCH %s:%s (FLAGS)\r\n",
					       info[0]->uid, info[1]->uid);
		spruce_folder_summary_info_unref (summary, info[1]);
	} else {
		ic = spruce_imap_engine_queue (engine, folder, "UID FETCH %s (FLAGS)\r\n",
					       info[0]->uid);
	}
	
	spruce_folder_summary_info_unref (summary, info[0]);
	
	spruce_imap_command_register_untagged (ic, "FETCH", untagged_fetch_all);
	ic->user_data = fetch;
	
	return ic;
}

static int
imap_summary_load (SpruceFolderSummary *summary)
{
	return 0;
}

static int
imap_summary_save (SpruceFolderSummary *summary)
{
	return SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->summary_save (summary);
}

static SpruceMessageInfo *
imap_message_info_new (SpruceFolderSummary *summary)
{
	SpruceMessageInfo *info;
	
	info = SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->message_info_new (summary);
	
	((SpruceIMAPMessageInfo *) info)->server_flags = 0;
	
	return info;
}

static SpruceMessageInfo *
imap_message_info_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceIMAPMessageInfo *minfo;
	SpruceMessageInfo *info;
	
	if (!(info = SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->message_info_load (summary, stream)))
		return NULL;
	
	minfo = (SpruceIMAPMessageInfo *) info;
	
	if (spruce_file_util_decode_uint32 (stream, &minfo->server_flags) == -1)
		goto exception;
	
	return info;
	
 exception:
	
	spruce_folder_summary_info_unref (summary, info);
	
	return NULL;
}

static int
imap_message_info_save (SpruceFolderSummary *summary, GMimeStream *stream, SpruceMessageInfo *info)
{
	SpruceIMAPMessageInfo *minfo = (SpruceIMAPMessageInfo *) info;
	
	if (SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->message_info_save (summary, stream, info) == -1)
		return -1;
	
	if (spruce_file_util_encode_uint32 (stream, minfo->server_flags) == -1)
		return -1;
	
	return 0;
}


void
spruce_imap_summary_set_exists (SpruceFolderSummary *summary, guint32 exists)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	
	g_return_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary));
	
	imap_summary->exists = exists;
}

void
spruce_imap_summary_set_recent (SpruceFolderSummary *summary, guint32 recent)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	
	g_return_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary));
	
	imap_summary->recent = recent;
}

void
spruce_imap_summary_set_unseen (SpruceFolderSummary *summary, guint32 unseen)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	
	g_return_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary));
	
	imap_summary->unseen = unseen;
}

void
spruce_imap_summary_set_uidnext (SpruceFolderSummary *summary, guint32 uidnext)
{
	g_return_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary));
	
	summary->nextuid = uidnext;
}

void
spruce_imap_summary_set_uidvalidity (SpruceFolderSummary *summary, guint32 uidvalidity)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	SpruceCache *cache = ((SpruceIMAPFolder *) imap_summary->folder)->cache;
	SpruceFolderChangeInfo *changes;
	SpruceMessageInfo *info;
	int count, i;
	
	g_return_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary));
	
	if (imap_summary->uidvalidity == uidvalidity)
		return;
	
	if ((count = spruce_folder_summary_count (summary)) > 0) {
		changes = spruce_folder_change_info_new ();
		for (i = 0; i < count; i++) {
			if (!(info = spruce_folder_summary_index (summary, i)))
				continue;
			
			spruce_folder_change_info_remove_uid (changes, info->uid);
			
			spruce_folder_summary_info_unref (summary, info);
		}
		
		spruce_folder_summary_clear (summary);
		
		if (spruce_folder_change_info_changed (changes))
			g_signal_emit_by_name (imap_summary->folder, "folder-changed", changes);
		spruce_folder_change_info_free (changes);
	}
	
	spruce_cache_expire_all (cache, NULL);
	
	imap_summary->uidvalidity = uidvalidity;
	
	imap_summary->uidvalidity_changed = TRUE;
}

void
spruce_imap_summary_expunge (SpruceFolderSummary *summary, int seqid)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	SpruceCache *cache = ((SpruceIMAPFolder *) imap_summary->folder)->cache;
	SpruceFolderChangeInfo *changes;
	SpruceMessageInfo *info;
	
	g_return_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary));
	
	seqid--;
	if (!(info = spruce_folder_summary_index (summary, seqid)))
		return;
	
	imap_summary->exists--;
	
	changes = spruce_folder_change_info_new ();
	spruce_folder_change_info_remove_uid (changes, info->uid);
	
	spruce_cache_expire_key (cache, info->uid, NULL);
	
	spruce_folder_summary_info_unref (summary, info);
	spruce_folder_summary_remove_index (summary, seqid);
	
	g_signal_emit_by_name (imap_summary->folder, "folder-changed", changes);
	spruce_folder_change_info_free (changes);
}

#if 0
static int
info_uid_sort (const SpruceMessageInfo **info0, const SpruceMessageInfo **info1)
{
	guint32 uid0, uid1;
	
	uid0 = strtoul ((*info0)->uid, NULL, 10);
	uid1 = strtoul ((*info1)->uid, NULL, 10);
	
	if (uid0 == uid1)
		return 0;
	
	return uid0 < uid1 ? -1 : 1;
}
#endif

int
spruce_imap_summary_flush_updates (SpruceFolderSummary *summary, GError **err)
{
	SpruceIMAPSummary *imap_summary = (SpruceIMAPSummary *) summary;
	SpruceIMAPEngine *engine;
	SpruceMessageInfo *info;
	SpruceIMAPCommand *ic;
	guint32 iuid, seqid = 0;
	int scount, id;
	char uid[16];
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_SUMMARY (summary), -1);
	
	engine = ((SpruceIMAPStore *) imap_summary->folder->store)->engine;
	if ((scount = spruce_folder_summary_count (summary))== 0)
		imap_summary->update_flags = FALSE;
	
	/* FIXME: take advantage of rfc4551's CHANGEDSINCE resync features */
	
	if (imap_summary->uidvalidity_changed) {
		/* need to refetch everything */
		g_assert (scount == 0);
		seqid = 1;
	} else if (imap_summary->update_flags || imap_summary->exists < scount) {
		/* this both updates flags and removes messages which
		 * have since been expunged from the server by another
		 * client */
		ic = imap_summary_fetch_flags (summary);
		
		while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
			;
		
		if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
			imap_fetch_all_free (ic->user_data);
			g_propagate_error (err, ic->err);
			ic->err = NULL;
			spruce_imap_command_unref (ic);
			return -1;
		}
		
		imap_fetch_all_update (ic->user_data);
		spruce_imap_command_unref (ic);
		
		scount = spruce_folder_summary_count (summary);
		if (imap_summary->exists < scount) {
			/* broken server? wtf? this should never happen... */
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_PROTOCOL_ERROR,
				     _("IMAP server %s is in an inconsistant state."),
				     engine->url->host);
			return -1;
		} else if (imap_summary->exists > scount) {
			/* need to fetch new envelopes */
			seqid = scount + 1;
		} else {
			/* we are fully synced */
			seqid = 0;
		}
	} else {
		/* need to fetch new envelopes */
		seqid = scount + 1;
	}
	
	if (seqid != 0 && seqid <= imap_summary->exists) {
		if (scount > 0) {
			info = spruce_folder_summary_index (summary, scount - 1);
			iuid = strtoul (info->uid, NULL, 10);
			spruce_folder_summary_info_unref (summary, info);
			sprintf (uid, "%u", iuid + 1);
		} else {
			strcpy (uid, "1");
		}
		
		ic = imap_summary_fetch_all (summary, seqid, uid);
		
		while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
			;
		
		if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
			imap_fetch_all_free (ic->user_data);
			g_propagate_error (err, ic->err);
			ic->err = NULL;
			spruce_imap_command_unref (ic);
			return -1;
		}
		
		imap_fetch_all_add (ic->user_data, TRUE);
		spruce_imap_command_unref (ic);
	}
	
	imap_summary->update_flags = FALSE;
	imap_summary->uidvalidity_changed = FALSE;
	
	return 0;
}
