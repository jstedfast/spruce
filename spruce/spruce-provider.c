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
#include <dirent.h>

#include <gmodule.h>
#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-service.h>
#include <spruce/spruce-provider.h>


static void spruce_provider_class_init (SpruceProviderClass *klass);
static void spruce_provider_init (SpruceProvider *provider, SpruceProviderClass *klass);
static void spruce_provider_finalize (GObject *object);


static GObjectClass *parent_class = NULL;


GType
spruce_provider_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceProviderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_provider_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceProvider),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_provider_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceProvider", &info, 0);
	}
	
	return type;
}


static void
spruce_provider_class_init (SpruceProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_provider_finalize;
}

static void
spruce_provider_init (SpruceProvider *provider, SpruceProviderClass *klass)
{
	;
}

static void
spruce_provider_finalize (GObject *object)
{
	SpruceProvider *provider = (SpruceProvider *) object;
	
	if (provider->authtypes)
		g_list_free (provider->authtypes);
	
	g_hash_table_destroy (provider->service_hash[0]);
	g_hash_table_destroy (provider->service_hash[1]);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


struct _ProviderModule {
	GModule *module;
	char **protocols;
	char *path;
	int loaded;
};

static GSList *modules_list;
static GHashTable *module_hash;
static GHashTable *provider_hash;

typedef void (* SpruceProviderModuleInitFunc) (void);
typedef void (* SpruceProviderModuleShutdownFunc) (void);

static void
provider_free (gpointer key, gpointer value, gpointer user_data)
{
	SpruceProvider *provider = value;
	struct _ProviderModule *module;
	gpointer symbol;
	
	if ((module = g_hash_table_lookup (module_hash, provider->protocol))) {
		if (g_module_symbol (module->module, "spruce_provider_module_shutdown", &symbol))
			((SpruceProviderModuleShutdownFunc) symbol) ();
	}
	
	g_object_unref (provider);
}


void
spruce_provider_shutdown (void)
{
	struct _ProviderModule *module;
	GSList *node, *next;
	
	if (!modules_list)
		return;
	
	g_hash_table_foreach (provider_hash, (GHFunc) provider_free, NULL);
	g_hash_table_destroy (provider_hash);
	
	node = modules_list;
	while (node != NULL) {
		next = node->next;
		module = node->data;
		g_slist_free_1 (node);
		/* Note: closing the module means that valgrind leak stack dumps will be useless */
		/*if (module->module)
		  g_module_close (module->module);*/
		g_strfreev (module->protocols);
		g_free (module->path);
		g_free (module);
		node = next;
	}
	
	g_hash_table_destroy (module_hash);
	
	provider_hash = NULL;
	modules_list = NULL;
	module_hash = NULL;
}

void
spruce_provider_scan_modules (void)
{
	struct dirent *dent;
	DIR *dir;
	
	module_hash = g_hash_table_new (g_str_hash, g_str_equal);
	provider_hash = g_hash_table_new (g_str_hash, g_str_equal);
	
	if (!(dir = opendir (SPRUCE_PROVIDERDIR)))
		return;
	
	while ((dent = readdir (dir))) {
		struct _ProviderModule *module;
		char linebuf[80], *ext, *path;
		GPtrArray *protocols;
		FILE *fp;
		
		if (!(ext = strrchr (dent->d_name, '.')) || strcmp (ext, ".urls") != 0)
			continue;
		
		path = g_build_filename (SPRUCE_PROVIDERDIR, dent->d_name, NULL);
		
		if (!(fp = fopen (path, "rt"))) {
			g_free (path);
			continue;
		}
		
		ext = strrchr (path, '.');
		ext++;
		strcpy (ext, G_MODULE_SUFFIX);
		
		protocols = g_ptr_array_new ();
		
		while (fgets (linebuf, sizeof (linebuf), fp)) {
			g_strstrip (linebuf);
			if (linebuf[0])
				g_ptr_array_add (protocols, g_strdup (linebuf));
		}
		
		if (protocols->len > 0) {
			int i;
			
			g_ptr_array_add (protocols, NULL);
			module = g_malloc (sizeof (struct _ProviderModule));
			module->protocols = (char **) protocols->pdata;
			module->path = path;
			module->loaded = FALSE;
			module->module = NULL;
			
			modules_list = g_slist_prepend (modules_list, module);
			
			for (i = 0; i < protocols->len - 1; i++)
				g_hash_table_insert (module_hash, module->protocols[i], module);
			
			g_ptr_array_free (protocols, FALSE);
		} else {
			g_ptr_array_free (protocols, TRUE);
		}
		
		fclose (fp);
	}
	
	closedir (dir);
}


void
spruce_provider_register (SpruceProvider *provider)
{
	g_return_if_fail (SPRUCE_IS_PROVIDER (provider));
	
	if (g_hash_table_lookup (provider_hash, provider->protocol))
		return;
	
	provider->service_hash[0] = g_hash_table_new (provider->url_hash, provider->url_equal);
	provider->service_hash[1] = g_hash_table_new (provider->url_hash, provider->url_equal);
	
	g_hash_table_insert (provider_hash, (void *) provider->protocol, provider);
	g_object_ref (provider);
}


static int
spruce_provider_load (struct _ProviderModule *plugin, GError **err)
{
	GModule *module;
	gpointer symbol;
	
	if (plugin->loaded)
		return 0;
	
	if (!g_module_supported ()) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Could not load %s: Module loading "
			       "not supported on this system."),
			     plugin->path);
		return -1;
	}
	
	if (!(module = g_module_open (plugin->path, 0))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Could not load %s: %s"),
			     plugin->path, g_module_error ());
		return -1;
	}
	
	if (!g_module_symbol (module, "spruce_provider_module_init", &symbol)) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Could not load %s: No initialization "
			       "code in module."), plugin->path);
		g_module_close (module);
		return -1;
	}
	
	((SpruceProviderModuleInitFunc) symbol) ();
	
	plugin->module = module;
	plugin->loaded = TRUE;
	
	return 0;
}


static void
list_add (gpointer key, gpointer value, gpointer user_data)
{
	GList **list = user_data;
	
	*list = g_list_prepend (*list, value);
}

static int
provider_compare (gconstpointer v0, gconstpointer v1)
{
	return strcmp (((SpruceProvider *) v0)->protocol, ((SpruceProvider *) v1)->protocol);
}


GList *
spruce_provider_list (gboolean load)
{
	GList *list = NULL;
	
	if (load) {
		struct _ProviderModule *module;
		GList *next;
		
		g_hash_table_foreach (module_hash, list_add, &list);
		
		while (list) {
			module = list->data;
			spruce_provider_load (module, NULL);
			next = list->next;
			g_list_free_1 (list);
			list = next;
		}
	}
	
	g_hash_table_foreach (provider_hash, list_add, &list);
	list = g_list_sort (list, provider_compare);
	
	return list;
}


SpruceProvider *
spruce_provider_lookup (const char *protocol, GError **err)
{
	SpruceProvider *provider;
	
	if (!(provider = g_hash_table_lookup (provider_hash, protocol))) {
		struct _ProviderModule *module;
		
		if (!(module = g_hash_table_lookup (module_hash, protocol))) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Could not get provider for %s: no module available"), protocol);
			return NULL;
		}
		
		if (spruce_provider_load (module, err) != -1) {
			if (!(provider = g_hash_table_lookup (provider_hash, protocol))) {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
					     _("Could not get provider for %s: no provider available"), protocol);
				return NULL;
			}
		}
	}
	
	return provider;
}


SpruceService *
spruce_provider_lookup_service (SpruceProvider *provider, SpruceURL *url, int type)
{
	SpruceService *service;
	
	g_return_val_if_fail (SPRUCE_IS_PROVIDER (provider), NULL);
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	g_return_val_if_fail (type >= 0 && type <= SPRUCE_NUM_PROVIDER_TYPES, NULL);
	
	if ((service = g_hash_table_lookup (provider->service_hash[type], url)))
		g_object_ref (service);
	
	return service;
}


static void
service_uncache (SpruceProvider *provider, SpruceService *service, int type)
{
	g_hash_table_remove (provider->service_hash[type], service);
}

static void
store_uncache (SpruceProvider *provider, SpruceService *service)
{
	service_uncache (provider, service, SPRUCE_PROVIDER_TYPE_STORE);
}

static void
transport_uncache (SpruceProvider *provider, SpruceService *service)
{
	service_uncache (provider, service, SPRUCE_PROVIDER_TYPE_TRANSPORT);
}


void
spruce_provider_insert_service (SpruceProvider *provider, SpruceService *service, int type)
{
	g_return_if_fail (SPRUCE_IS_PROVIDER (provider));
	g_return_if_fail (SPRUCE_IS_SERVICE (service));
	g_return_if_fail (type >= 0 && type <= SPRUCE_NUM_PROVIDER_TYPES);
	
	g_hash_table_insert (provider->service_hash[type], service->url, service);
	
	switch (type) {
	case SPRUCE_PROVIDER_TYPE_STORE:
		g_object_weak_ref ((GObject *) service, (GWeakNotify) store_uncache, provider);
		break;
	case SPRUCE_PROVIDER_TYPE_TRANSPORT:
		g_object_weak_ref ((GObject *) service, (GWeakNotify) transport_uncache, provider);
		break;
	}
}
