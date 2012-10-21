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
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <spruce/spruce-error.h>

#include "spruce-imap-engine.h"
#include "spruce-imap-stream.h"
#include "spruce-imap-command.h"

#include "spruce-imap-utils.h"

#define d(x) x


static char *utf7_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

static unsigned char utf7_rank[256] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x3e,0x3f,0xff,0xff,0xff,
	0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,
	0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0xff,0xff,0xff,0xff,0xff,
	0xff,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
	0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};

static inline void
spruce_utf8_putc (unsigned char **outbuf, gunichar u)
{
	register unsigned char *outptr = *outbuf;
	
	if (u <= 0x7f)
		*outptr++ = u;
	else if (u <= 0x7ff) {
		*outptr++ = 0xc0 | u >> 6;
		*outptr++ = 0x80 | (u & 0x3f);
	} else if (u <= 0xffff) {
		*outptr++ = 0xe0 | u >> 12;
		*outptr++ = 0x80 | ((u >> 6) & 0x3f);
		*outptr++ = 0x80 | (u & 0x3f);
	} else {
		/* see unicode standard 3.0, S 3.8, max 4 octets */
		*outptr++ = 0xf0 | u >> 18;
		*outptr++ = 0x80 | ((u >> 12) & 0x3f);
		*outptr++ = 0x80 | ((u >> 6) & 0x3f);
		*outptr++ = 0x80 | (u & 0x3f);
	}
	
	*outbuf = outptr;
}

char *
spruce_imap_utf7_utf8 (const char *in)
{
	const unsigned char *inptr = (unsigned char *) in;
	unsigned char c;
	int shifted = 0;
	guint32 v = 0;
	GString *out;
	gunichar u;
	char *buf;
	int i = 0;
	
	out = g_string_new ("");
	
	while (*inptr) {
		c = *inptr++;
		
		if (shifted) {
			if (c == '-') {
				/* shifted back to US-ASCII */
				shifted = 0;
			} else {
				/* base64 decode */
				if (utf7_rank[c] == 0xff)
					goto exception;
				
				v = (v << 6) | utf7_rank[c];
				i += 6;
				
				if (i >= 16) {
					u = (v >> (i - 16)) & 0xffff;
					g_string_append_unichar (out, u);
					i -= 16;
				}
			}
		} else if (c == '&') {
			if (*inptr == '-') {
				g_string_append_c (out, '&');
				inptr++;
			} else {
				/* shifted to modified UTF-7 */
				shifted = 1;
			}
		} else {
			g_string_append_c (out, c);
		}
	}
	
	if (shifted) {
	exception:
		g_warning ("Invalid UTF-7 encoded string: '%s'", in);
		g_string_free (out, TRUE);
		return g_strdup (in);
	}
	
	buf = out->str;
	g_string_free (out, FALSE);
	
	return buf;
}


static inline gunichar
spruce_utf8_getc (const unsigned char **in)
{
	register const unsigned char *inptr = *in;
	register unsigned char c, r;
	register gunichar m, u = 0;
	
	if (*inptr == '\0')
		return 0;
	
	r = *inptr++;
	if (r < 0x80) {
		*in = inptr;
		u = r;
	} else if (r < 0xfe) { /* valid start char? */
		u = r;
		m = 0x7f80;	/* used to mask out the length bits */
		do {
			c = *inptr++;
			if ((c & 0xc0) != 0x80)
				goto error;
			
			u = (u << 6) | (c & 0x3f);
			r <<= 1;
			m <<= 5;
		} while (r & 0x40);
		
		*in = inptr;
		
		u &= ~m;
	} else {
	error:
		*in = (*in) + 1;
		u = 0xfffe;
	}
	
	return u;
}

static void
utf7_close (GString *out, guint32 u2, int i)
{
	guint32 x;
	
	if (i > 0) {
		x = (u2 << (6 - i)) & 0x3f;
		g_string_append_c (out, utf7_alphabet[x]);
	}
	
	g_string_append_c (out, '-');
}

char *
spruce_imap_utf8_utf7 (const char *in)
{
	const unsigned char *inbuf = (unsigned char *) in;
	const unsigned char *inptr = (unsigned char *) in;
	guint32 x, u2 = 0;
	int shifted = 0;
	GString *out;
	gunichar u;
	char *buf;
	int i = 0;
	
	out = g_string_new ("");
	while ((u = spruce_utf8_getc (&inptr))) {
		if (u == 0xfffe) {
			char *where;
			
			where = g_alloca (inptr - inbuf);
			memcpy (where, "-", (inptr - inbuf) - 1);
			where[(inptr - inbuf) - 1] = '\0';
			
			g_warning ("Invalid UTF-8 sequence encountered in '%s'\n"
				   "                                       %s^", in, where);
			continue;
		}
		
		if (u >= 0x20 && u <= 0x7e) {
			/* characters with octet values 0x20-0x25 and 0x27-0x7e
			   represent themselves while 0x26 ("&") is represented
			   by the two-octet sequence "&-" */
			if (shifted) {
				utf7_close (out, u2, i);
				shifted = 0;
				i = 0;
			}
			
			if (u == 0x26)
				g_string_append (out, "&-");
			else
				g_string_append_c (out, (char) u);
		} else {
			/* base64 encode */
			if (!shifted) {
				g_string_append_c (out, '&');
				shifted = 1;
			}
			
			u2 = (u2 << 16) | (u & 0xffff);
			i += 16;
			
			while (i >= 6) {
				x = (u2 >> (i - 6)) & 0x3f;
				g_string_append_c (out, utf7_alphabet[x]);
				i -= 6;
			}
		}
	}
	
	if (shifted)
		utf7_close (out, u2, i);
	
	buf = out->str;
	g_string_free (out, FALSE);
	
	return buf;
}


void
spruce_imap_flags_diff (flags_diff_t *diff, guint32 old, guint32 new)
{
	diff->changed = old ^ new;
	diff->bits = new & diff->changed;
}


guint32
spruce_imap_flags_merge (flags_diff_t *diff, guint32 flags)
{
	return (flags & ~diff->changed) | diff->bits;
}


/**
 * spruce_imap_merge_flags:
 * @original: original server flags
 * @local: local flags (after changes)
 * @server: new server flags (another client updated the server flags)
 *
 * Merge the local flag changes into the new server flags.
 *
 * Returns the merged flags.
 **/
guint32
spruce_imap_merge_flags (guint32 original, guint32 local, guint32 server)
{
	flags_diff_t diff;
	
	spruce_imap_flags_diff (&diff, original, local);
	
	return spruce_imap_flags_merge (&diff, server);
}


struct _uidset_range {
	struct _uidset_range *next;
	guint32 first, last;
	uint8_t buflen;
	char buf[24];
};

struct _uidset {
	SpruceFolderSummary *summary;
	struct _uidset_range *ranges;
	struct _uidset_range *tail;
	size_t maxlen, setlen;
};

static void
uidset_range_free (struct _uidset_range *range)
{
	struct _uidset_range *next;
	
	while (range != NULL) {
		next = range->next;
		g_free (range);
		range = next;
	}
}

static void
uidset_init (struct _uidset *uidset, SpruceFolderSummary *summary, size_t maxlen)
{
	uidset->ranges = g_new (struct _uidset_range, 1);
	uidset->ranges->first = (guint32) -1;
	uidset->ranges->last = (guint32) -1;
	uidset->ranges->next = NULL;
	uidset->ranges->buflen = 0;
	
	uidset->tail = uidset->ranges;
	uidset->summary = summary;
	uidset->maxlen = maxlen;
	uidset->setlen = 0;
}

/* returns: -1 on full-and-not-added, 0 on added-and-not-full or 1 on added-and-full */
static int
uidset_add (struct _uidset *uidset, SpruceMessageInfo *info)
{
	GPtrArray *messages = uidset->summary->messages;
	struct _uidset_range *node, *tail = uidset->tail;
	size_t uidlen, len;
	guint32 index;
	char *colon;
	
	/* Note: depends on integer overflow for initial 'add' */
	for (index = tail->last + 1; index < messages->len; index++) {
		if (info == messages->pdata[index])
			break;
	}
	
	g_assert (index < messages->len);
	
	uidlen = strlen (info->uid);
	
	if (tail->buflen == 0) {
		/* first add */
		tail->first = tail->last = index;
		strcpy (tail->buf, info->uid);
		uidset->setlen = uidlen;
		tail->buflen = uidlen;
	} else if (index == (tail->last + 1)) {
		/* add to last range */
		if (tail->last == tail->first) {
			/* make sure we've got enough room to add this one... */
			if ((uidset->setlen + uidlen + 1) > uidset->maxlen)
				return -1;
			
			tail->buf[tail->buflen++] = ':';
			uidset->setlen++;
		} else {
			colon = strchr (tail->buf, ':') + 1;
			
			len = strlen (colon);
			uidset->setlen -= len;
			tail->buflen -= len;
		}
		
		strcpy (tail->buf + tail->buflen, info->uid);
		uidset->setlen += uidlen;
		tail->buflen += uidlen;
		
		tail->last = index;
	} else if ((uidset->setlen + uidlen + 1) < uidset->maxlen) {
		/* the beginning of a new range */
		tail->next = node = g_new (struct _uidset_range, 1);
		node->first = node->last = index;
		strcpy (node->buf, info->uid);
		uidset->setlen += uidlen + 1;
		node->buflen = uidlen;
		uidset->tail = node;
		node->next = NULL;
	} else {
		/* can't add this one... */
		return -1;
	}
	
	if (uidset->setlen < uidset->maxlen)
		return 0;
	
	return 1;
}

static char *
uidset_to_string (struct _uidset *uidset)
{
	struct _uidset_range *range;
	GString *string;
	char *str;
	
	string = g_string_new ("");
	
	range = uidset->ranges;
	while (range != NULL) {
		g_string_append (string, range->buf);
		range = range->next;
		if (range)
			g_string_append_c (string, ',');
	}
	
	str = string->str;
	g_string_free (string, FALSE);
	
	return str;
}


int
spruce_imap_get_uid_set (SpruceIMAPEngine *engine, SpruceFolderSummary *summary, GPtrArray *infos, int cur, size_t linelen, char **set)
{
	struct _uidset uidset;
	size_t maxlen;
	int rv = 0;
	int i;
	
	if (engine->maxlentype == SPRUCE_IMAP_ENGINE_MAXLEN_LINE)
		maxlen = engine->maxlen - linelen;
	else
		maxlen = engine->maxlen;
	
	uidset_init (&uidset, summary, maxlen);
	
	for (i = cur; i < infos->len && rv != 1; i++) {
		if ((rv = uidset_add (&uidset, infos->pdata[i])) == -1)
			break;
	}
	
	if (i > cur)
		*set = uidset_to_string (&uidset);
	
	uidset_range_free (uidset.ranges);
	
	return (i - cur);
}


void
spruce_imap_utils_set_unexpected_token_error (GError **err, SpruceIMAPEngine *engine, spruce_imap_token_t *token)
{
	GString *errmsg;
	
	if (err == NULL)
		return;
	
	errmsg = g_string_new ("");
	g_string_append_printf (errmsg, _("Unexpected token in response from IMAP server %s: "),
				engine->url->host);
	
	switch (token->token) {
	case SPRUCE_IMAP_TOKEN_NIL:
		g_string_append (errmsg, "NIL");
		break;
	case SPRUCE_IMAP_TOKEN_ATOM:
		g_string_append (errmsg, token->v.atom);
		break;
	case SPRUCE_IMAP_TOKEN_FLAG:
		g_string_append (errmsg, token->v.flag);
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
		g_string_append (errmsg, token->v.qstring);
		break;
	case SPRUCE_IMAP_TOKEN_LITERAL:
		g_string_append_printf (errmsg, "{%zu}", token->v.literal);
		break;
	case SPRUCE_IMAP_TOKEN_NUMBER:
		g_string_append_printf (errmsg, "%" G_GUINT32_FORMAT, token->v.number);
		break;
	case SPRUCE_IMAP_TOKEN_NUMBER64:
		g_string_append_printf (errmsg, "%" G_GUINT64_FORMAT, token->v.number64);
		break;
	case SPRUCE_IMAP_TOKEN_NO_DATA:
		g_string_append (errmsg, _("No data"));
		break;
	default:
		g_string_append_c (errmsg, (unsigned char) (token->token & 0xff));
		break;
	}
	
	g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC, "%s", errmsg->str);
	
	g_string_free (errmsg, TRUE);
}


static struct {
	const char *name;
	guint32 flag;
} imap_flags[] = {
	{ "\\Answered", SPRUCE_MESSAGE_ANSWERED   },
	{ "\\Deleted",  SPRUCE_MESSAGE_DELETED    },
	{ "\\Draft",    SPRUCE_MESSAGE_DRAFT      },
	{ "\\Flagged",  SPRUCE_MESSAGE_FLAGGED    },
	{ "\\Seen",     SPRUCE_MESSAGE_SEEN       },
	{ "\\Recent",   SPRUCE_MESSAGE_RECENT     },
	{ "\\*",        SPRUCE_MESSAGE_USER_FLAGS },
	
	/* user-defined flags */
	{ "Forwarded",  SPRUCE_MESSAGE_FORWARDED  },
	{ "NonJunk",    SPRUCE_MESSAGE_NOTJUNK    },
	{ "Junk",       SPRUCE_MESSAGE_JUNK       },
};


int
spruce_imap_parse_flags_list (SpruceIMAPEngine *engine, guint32 *flags, GError **err)
{
	spruce_imap_token_t token;
	guint32 new = 0;
	int i;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token != '(') {
		d(fprintf (stderr, "Expected to find a '(' token starting the flags list\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	while (token.token == SPRUCE_IMAP_TOKEN_ATOM || token.token == SPRUCE_IMAP_TOKEN_FLAG) {
		/* parse the flags list */
		for (i = 0; i < G_N_ELEMENTS (imap_flags); i++) {
			if (!g_ascii_strcasecmp (imap_flags[i].name, token.v.atom)) {
				new |= imap_flags[i].flag;
				break;
			}
		}
		
		if (i == G_N_ELEMENTS (imap_flags))
			fprintf (stderr, "Encountered unknown flag: %s\n", token.v.atom);
		
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
	}
	
	if (token.token != ')') {
		d(fprintf (stderr, "Expected to find a ')' token terminating the flags list\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	*flags = new;
	
	return 0;
}


struct {
	const char *name;
	guint32 flag;
} list_flags[] = {
	{ "\\Marked",        SPRUCE_IMAP_FOLDER_MARKED          },
	{ "\\Unmarked",      SPRUCE_IMAP_FOLDER_UNMARKED        },
	{ "\\NoSelect",      SPRUCE_IMAP_FOLDER_NOSELECT        },
	{ "\\NoInferiors",   SPRUCE_IMAP_FOLDER_NOINFERIORS     },
	{ "\\HasChildren",   SPRUCE_IMAP_FOLDER_HAS_CHILDREN    },
	{ "\\HasNoChildren", SPRUCE_IMAP_FOLDER_HAS_NO_CHILDREN },
};

int
spruce_imap_untagged_list (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, guint32 index, spruce_imap_token_t *token, GError **err)
{
	GPtrArray *array = ic->user_data;
	spruce_imap_list_t *list;
	unsigned char *buf;
	guint32 flags = 0;
	GString *literal;
	char delim;
	size_t n;
	int i;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	/* parse the flag list */
	if (token->token != '(')
		goto unexpected;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	while (token->token == SPRUCE_IMAP_TOKEN_FLAG || token->token == SPRUCE_IMAP_TOKEN_ATOM) {
		for (i = 0; i < G_N_ELEMENTS (list_flags); i++) {
			if (!g_ascii_strcasecmp (list_flags[i].name, token->v.atom)) {
				flags |= list_flags[i].flag;
				break;
			}
		}
		
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			return -1;
	}
	
	if (token->token != ')')
		goto unexpected;
	
	/* parse the path delimiter */
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	switch (token->token) {
	case SPRUCE_IMAP_TOKEN_NIL:
		delim = '\0';
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
		delim = *token->v.qstring;
		break;
	default:
		goto unexpected;
	}
	
	/* parse the folder name */
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	list = g_new (spruce_imap_list_t, 1);
	list->flags = flags;
	list->delim = delim;
	
	switch (token->token) {
	case SPRUCE_IMAP_TOKEN_ATOM:
		list->name = g_strdup (token->v.atom);
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
		list->name = g_strdup (token->v.qstring);
		break;
	case SPRUCE_IMAP_TOKEN_LITERAL:
		literal = g_string_new ("");
		while ((i = spruce_imap_stream_literal (engine->istream, &buf, &n)) == 1)
			g_string_append_len (literal, (char *) buf, n);
		
		if (i == -1) {
			g_set_error (err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
				     _("IMAP server %s unexpectedly disconnected: %s"),
				     engine->url->host, errno ? g_strerror (errno) : _("Unknown"));
			g_string_free (literal, TRUE);
			return -1;
		}
		
		g_string_append_len (literal, (char *) buf, n);
		list->name = literal->str;
		g_string_free (literal, FALSE);
		break;
	default:
		g_free (list);
		goto unexpected;
	}
	
	g_ptr_array_add (array, list);
	
	return spruce_imap_engine_eat_line (engine, err);
	
 unexpected:
	
	spruce_imap_utils_set_unexpected_token_error (err, engine, token);
	
	return -1;
}


static struct {
	const char *name;
	int type;
} imap_status[] = {
	{ "MESSAGES",    SPRUCE_IMAP_STATUS_MESSAGES    },
	{ "RECENT",      SPRUCE_IMAP_STATUS_RECENT      },
	{ "UIDNEXT",     SPRUCE_IMAP_STATUS_UIDNEXT     },
	{ "UIDVALIDITY", SPRUCE_IMAP_STATUS_UIDVALIDITY },
	{ "UNSEEN",      SPRUCE_IMAP_STATUS_UNSEEN      },
};


void
spruce_imap_status_free (spruce_imap_status_t *status)
{
	spruce_imap_status_attr_t *attr, *next;
	
	attr = status->attr_list;
	while (attr != NULL) {
		next = attr->next;
		g_free (attr);
		attr = next;
	}
	
	g_free (status->mailbox);
	g_free (status);
}


int
spruce_imap_untagged_status (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, guint32 index, spruce_imap_token_t *token, GError **err)
{
	spruce_imap_status_attr_t *attr, *tail, *list = NULL;
	GPtrArray *array = ic->user_data;
	spruce_imap_status_t *status;
	unsigned char *literal;
	char *mailbox;
	size_t len;
	int type;
	int i;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	switch (token->token) {
	case SPRUCE_IMAP_TOKEN_ATOM:
		mailbox = g_strdup (token->v.atom);
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
		mailbox = g_strdup (token->v.qstring);
		break;
	case SPRUCE_IMAP_TOKEN_LITERAL:
		if (spruce_imap_engine_literal (engine, &literal, &len, err) == -1)
			return -1;
		
		mailbox = (char *) literal;
		break;
	default:
		fprintf (stderr, "Unexpected token in IMAP untagged STATUS response: %s%c\n",
			 token->token == SPRUCE_IMAP_TOKEN_NIL ? "NIL" : "",
			 (unsigned char) (token->token & 0xff));
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		return -1;
	}
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1) {
		g_free (mailbox);
		return -1;
	}
	
	if (token->token != '(') {
		d(fprintf (stderr, "Expected to find a '(' token after the mailbox token in the STATUS response\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		g_free (mailbox);
		return -1;
	}
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1) {
		g_free (mailbox);
		return -1;
	}
	
	tail = (spruce_imap_status_attr_t *) &list;
	
	while (token->token == SPRUCE_IMAP_TOKEN_ATOM) {
		/* parse the status messages list */
		type = SPRUCE_IMAP_STATUS_UNKNOWN;
		for (i = 0; i < G_N_ELEMENTS (imap_status); i++) {
			if (!g_ascii_strcasecmp (imap_status[i].name, token->v.atom)) {
				type = imap_status[i].type;
				break;
			}
		}
		
		if (type == SPRUCE_IMAP_STATUS_UNKNOWN)
			d(fprintf (stderr, "unrecognized token in STATUS list: %s\n", token->v.atom));
		
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			goto exception;
		
		if (token->token != SPRUCE_IMAP_TOKEN_NUMBER)
			break;
		
		attr = g_new (spruce_imap_status_attr_t, 1);
		attr->next = NULL;
		attr->type = type;
		attr->value = token->v.number;
		
		tail->next = attr;
		tail = attr;
		
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			goto exception;
	}
	
	status = g_new (spruce_imap_status_t, 1);
	status->mailbox = mailbox;
	status->attr_list = list;
	list = NULL;
	
	g_ptr_array_add (array, status);
	
	if (token->token != ')') {
		d(fprintf (stderr, "Expected to find a ')' token terminating the untagged STATUS response\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		return -1;
	}
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	if (token->token != '\n') {
		d(fprintf (stderr, "Expected to find a '\\n' token after the STATUS response\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		return -1;
	}
	
	return 0;
	
 exception:
	
	g_free (mailbox);
	
	attr = list;
	while (attr != NULL) {
		list = attr->next;
		g_free (attr);
		attr = list;
	}
	
	return -1;
}
