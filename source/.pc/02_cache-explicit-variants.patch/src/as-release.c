/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2016 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C)      2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:as-release
 * @short_description: Object representing a single upstream release
 * @include: appstream.h
 *
 * This object represents a single upstream release, typically a minor update.
 * Releases can contain a localized description of paragraph and list elements
 * and also have a version number and timestamp.
 *
 * Releases can be automatically generated by parsing upstream ChangeLogs or
 * .spec files, or can be populated using MetaInfo files.
 *
 * See also: #AsComponent
 */

#include "as-release-private.h"

#include "as-utils.h"
#include "as-utils-private.h"
#include "as-checksum-private.h"
#include "as-variant-cache.h"

typedef struct
{
	AsReleaseKind	kind;
	gchar		*version;
	GHashTable	*description;
	guint64		timestamp;

	AsContext	*context;
	gchar		*active_locale_override;

	GPtrArray	*locations;
	GPtrArray	*checksums;
	guint64		size[AS_SIZE_KIND_LAST];

	AsUrgencyKind	urgency;
} AsReleasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (AsRelease, as_release, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (as_release_get_instance_private (o))

/**
 * as_release_kind_to_string:
 * @kind: the #AsReleaseKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.12.0
 **/
const gchar*
as_release_kind_to_string (AsReleaseKind kind)
{
	if (kind == AS_RELEASE_KIND_STABLE)
		return "stable";
	if (kind == AS_RELEASE_KIND_DEVELOPMENT)
		return "development";
	return "unknown";
}

/**
 * as_release_kind_from_string:
 * @kind_str: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: an #AsReleaseKind or %AS_RELEASE_KIND_UNKNOWN for unknown
 *
 * Since: 0.12.0
 **/
AsReleaseKind
as_release_kind_from_string (const gchar *kind_str)
{
	if (g_strcmp0 (kind_str, "stable") == 0)
		return AS_RELEASE_KIND_STABLE;
	if (g_strcmp0 (kind_str, "development") == 0)
		return AS_RELEASE_KIND_DEVELOPMENT;
	return AS_RELEASE_KIND_UNKNOWN;
}

/**
 * as_size_kind_to_string:
 * @size_kind: the #AsSizeKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @size_kind
 *
 * Since: 0.8.6
 **/
const gchar*
as_size_kind_to_string (AsSizeKind size_kind)
{
	if (size_kind == AS_SIZE_KIND_INSTALLED)
		return "installed";
	if (size_kind == AS_SIZE_KIND_DOWNLOAD)
		return "download";
	return "unknown";
}

/**
 * as_size_kind_from_string:
 * @size_kind: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: an #AsSizeKind or %AS_SIZE_KIND_UNKNOWN for unknown
 *
 * Since: 0.8.6
 **/
AsSizeKind
as_size_kind_from_string (const gchar *size_kind)
{
	if (g_strcmp0 (size_kind, "download") == 0)
		return AS_SIZE_KIND_DOWNLOAD;
	if (g_strcmp0 (size_kind, "installed") == 0)
		return AS_SIZE_KIND_INSTALLED;
	return AS_SIZE_KIND_UNKNOWN;
}

/**
 * as_release_init:
 **/
static void
as_release_init (AsRelease *release)
{
	guint i;
	AsReleasePrivate *priv = GET_PRIVATE (release);

	/* we assume a stable release by default */
	priv->kind = AS_RELEASE_KIND_STABLE;

	priv->description = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->locations = g_ptr_array_new_with_free_func (g_free);

	priv->checksums = g_ptr_array_new_with_free_func (g_object_unref);
	priv->urgency = AS_URGENCY_KIND_UNKNOWN;

	for (i = 0; i < AS_SIZE_KIND_LAST; i++)
		priv->size[i] = 0;
}

/**
 * as_release_finalize:
 **/
static void
as_release_finalize (GObject *object)
{
	AsRelease *release = AS_RELEASE (object);
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->version);
	g_free (priv->active_locale_override);
	g_hash_table_unref (priv->description);
	g_ptr_array_unref (priv->locations);
	g_ptr_array_unref (priv->checksums);
	if (priv->context != NULL)
		g_object_unref (priv->context);

	G_OBJECT_CLASS (as_release_parent_class)->finalize (object);
}

/**
 * as_release_class_init:
 **/
static void
as_release_class_init (AsReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = as_release_finalize;
}

/**
 * as_release_get_kind:
 * @release: a #AsRelease instance.
 *
 * Gets the type of the release.
 * (development or stable release)
 *
 * Since: 0.12.0
 **/
AsReleaseKind
as_release_get_kind (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->kind;
}

/**
 * as_release_set_kind:
 * @release: a #AsRelease instance.
 * @kind: the #AsReleaseKind
 *
 * Sets the release kind to distinguish between end-user ready
 * stable releases and development prereleases..
 *
 * Since: 0.12.0
 **/
void
as_release_set_kind (AsRelease *release, AsReleaseKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->kind = kind;
}

/**
 * as_release_get_version:
 * @release: a #AsRelease instance.
 *
 * Gets the release version.
 *
 * Returns: string, or %NULL for not set or invalid
 **/
const gchar*
as_release_get_version (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->version;
}

/**
 * as_release_set_version:
 * @release: a #AsRelease instance.
 * @version: the version string.
 *
 * Sets the release version.
 **/
void
as_release_set_version (AsRelease *release, const gchar *version)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_free (priv->version);
	priv->version = g_strdup (version);
}

/**
 * as_release_vercmp:
 * @rel1: an #AsRelease
 * @rel2: an #AsRelease
 *
 * Compare the version numbers of two releases.
 *
 * Returns: 1 if @rel1 version is higher than @rel2, 0 if versions are equal, -1 if @rel1 version is higher than @rel2.
 */
gint
as_release_vercmp (AsRelease *rel1, AsRelease *rel2)
{
	return as_utils_compare_versions (as_release_get_version (rel1),
					  as_release_get_version (rel2));
}

/**
 * as_release_get_timestamp:
 * @release: a #AsRelease instance.
 *
 * Gets the release timestamp.
 *
 * Returns: timestamp, or 0 for unset
 **/
guint64
as_release_get_timestamp (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->timestamp;
}

/**
 * as_release_set_timestamp:
 * @release: a #AsRelease instance.
 * @timestamp: the timestamp value.
 *
 * Sets the release timestamp.
 **/
void
as_release_set_timestamp (AsRelease *release, guint64 timestamp)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->timestamp = timestamp;
}

/**
 * as_release_get_urgency:
 * @release: a #AsRelease instance.
 *
 * Gets the urgency of the release
 * (showing how important it is to update to a more recent release)
 *
 * Returns: #AsUrgencyKind, or %AS_URGENCY_KIND_UNKNOWN for not set
 *
 * Since: 0.6.5
 **/
AsUrgencyKind
as_release_get_urgency (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->urgency;
}

/**
 * as_release_set_urgency:
 * @release: a #AsRelease instance.
 * @urgency: the urgency of this release/update (as #AsUrgencyKind)
 *
 * Sets the release urgency.
 *
 * Since: 0.6.5
 **/
void
as_release_set_urgency (AsRelease *release, AsUrgencyKind urgency)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->urgency = urgency;
}

/**
 * as_release_get_size:
 * @release: a #AsRelease instance
 * @kind: a #AsSizeKind
 *
 * Gets the release size.
 *
 * Returns: The size of the given kind of this release.
 *
 * Since: 0.8.6
 **/
guint64
as_release_get_size (AsRelease *release, AsSizeKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (kind < AS_SIZE_KIND_LAST, 0);
	return priv->size[kind];
}

/**
 * as_release_set_size:
 * @release: a #AsRelease instance
 * @size: a size in bytes, or 0 for unknown
 * @kind: a #AsSizeKind
 *
 * Sets the release size for the given kind.
 *
 * Since: 0.8.6
 **/
void
as_release_set_size (AsRelease *release, guint64 size, AsSizeKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (kind < AS_SIZE_KIND_LAST);
	g_return_if_fail (kind != 0);

	priv->size[kind] = size;
}

/**
 * as_release_get_description:
 * @release: a #AsRelease instance.
 *
 * Gets the release description markup for a given locale.
 *
 * Returns: markup, or %NULL for not set or invalid
 **/
const gchar*
as_release_get_description (AsRelease *release)
{
	const gchar *desc;
	AsReleasePrivate *priv = GET_PRIVATE (release);

	desc = g_hash_table_lookup (priv->description, as_release_get_active_locale (release));
	if (desc == NULL) {
		/* fall back to untranslated / default */
		desc = g_hash_table_lookup (priv->description, "C");
	}

	return desc;
}

/**
 * as_release_set_description:
 * @release: a #AsRelease instance.
 * @description: the description markup.
 *
 * Sets the description release markup.
 **/
void
as_release_set_description (AsRelease *release, const gchar *description, const gchar *locale)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	if (locale == NULL)
		locale = as_release_get_active_locale (release);

	g_hash_table_insert (priv->description,
				g_strdup (locale),
				g_strdup (description));
}

/**
 * as_release_get_active_locale:
 *
 * Get the current active locale, which
 * is used to get localized messages.
 */
const gchar*
as_release_get_active_locale (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	const gchar *locale;

	/* return context locale, if the locale isn't explicitly overridden for this component */
	if ((priv->context != NULL) && (priv->active_locale_override == NULL)) {
		locale = as_context_get_locale (priv->context);
	} else {
		locale = priv->active_locale_override;
	}

	if (locale == NULL)
		return "C";
	else
		return locale;
}

/**
 * as_release_set_active_locale:
 *
 * Set the current active locale, which
 * is used to get localized messages.
 * If the #AsComponent linking this #AsRelease was fetched
 * from a localized database, usually only
 * one locale is available.
 */
void
as_release_set_active_locale (AsRelease *release, const gchar *locale)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->active_locale_override);
	priv->active_locale_override = g_strdup (locale);
}

/**
 * as_release_get_locations:
 *
 * Gets the release locations, typically URLs.
 *
 * Returns: (transfer none) (element-type utf8): list of locations
 *
 * Since: 0.8.1
 **/
GPtrArray*
as_release_get_locations (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->locations;
}

/**
 * as_release_add_location:
 * @location: An URL of the download location
 *
 * Adds a release location.
 *
 * Since: 0.8.1
 **/
void
as_release_add_location (AsRelease *release, const gchar *location)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_ptr_array_add (priv->locations, g_strdup (location));
}

/**
 * as_release_get_checksums:
 *
 * Get a list of all checksums we have for this release.
 *
 * Returns: (transfer none) (element-type AsChecksum): an array of #AsChecksum objects.
 *
 * Since: 0.10
 **/
GPtrArray*
as_release_get_checksums (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->checksums;
}

/**
 * as_release_get_checksum:
 *
 * Gets the release checksum
 *
 * Returns: (transfer none): an #AsChecksum, or %NULL for not set or invalid
 *
 * Since: 0.8.2
 **/
AsChecksum*
as_release_get_checksum (AsRelease *release, AsChecksumKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	guint i;

	for (i = 0; i < priv->checksums->len; i++) {
		AsChecksum *cs = AS_CHECKSUM (g_ptr_array_index (priv->checksums, i));
		if (as_checksum_get_kind (cs) == kind)
			return cs;
	}
	return NULL;
}

/**
 * as_release_add_checksum:
 * @release: An instance of #AsRelease.
 * @cs: The #AsChecksum.
 *
 * Add a checksum for the file associated with this release.
 *
 * Since: 0.8.2
 */
void
as_release_add_checksum (AsRelease *release, AsChecksum *cs)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_ptr_array_add (priv->checksums, g_object_ref (cs));
}

/**
 * as_release_get_context:
 * @release: An instance of #AsRelease.
 *
 * Returns: the #AsContext associated with this release.
 * This function may return %NULL if no context is set.
 *
 * Since: 0.11.2
 */
AsContext*
as_release_get_context (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->context;
}

/**
 * as_release_set_context:
 * @release: An instance of #AsRelease.
 * @context: the #AsContext.
 *
 * Sets the document context this release is associated
 * with.
 *
 * Since: 0.11.2
 */
void
as_release_set_context (AsRelease *release, AsContext *context)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->context != NULL)
		g_object_unref (priv->context);
	priv->context = g_object_ref (context);

	/* reset individual properties, so the new context overrides them */
	g_free (priv->active_locale_override);
	priv->active_locale_override = NULL;
}

/**
 * as_release_parse_xml_metainfo_description_cb:
 *
 * Helper function for GHashTable
 */
static void
as_release_parse_xml_metainfo_description_cb (gchar *key, GString *value, AsRelease *rel)
{
	g_assert (AS_IS_RELEASE (rel));

	as_release_set_description (rel, value->str, key);
	g_string_free (value, TRUE);
}

/**
 * as_release_load_from_xml:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @node: the XML node.
 * @error: a #GError.
 *
 * Loads data from an XML node.
 **/
gboolean
as_release_load_from_xml (AsRelease *release, AsContext *ctx, xmlNode *node, GError **error)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	xmlNode *iter;
	gchar *prop;

	/* propagate context */
	as_release_set_context (release, ctx);

	prop = (gchar*) xmlGetProp (node, (xmlChar*) "type");
	if (prop != NULL) {
		priv->kind = as_release_kind_from_string (prop);
		g_free (prop);
	}

	prop = (gchar*) xmlGetProp (node, (xmlChar*) "version");
	as_release_set_version (release, prop);
	g_free (prop);

	prop = (gchar*) xmlGetProp (node, (xmlChar*) "date");
	if (prop != NULL) {
		g_autoptr(GDateTime) time;
		time = as_iso8601_to_datetime (prop);
		if (time != NULL) {
			priv->timestamp = g_date_time_to_unix (time);
		} else {
			g_debug ("Invalid ISO-8601 date in releases at %s line %li", as_context_get_filename (ctx), xmlGetLineNo (node));
		}
		g_free (prop);
	}

	prop = (gchar*) xmlGetProp (node, (xmlChar*) "timestamp");
	if (prop != NULL) {
		priv->timestamp = atol (prop);
		g_free (prop);
	}
	prop = (gchar*) xmlGetProp (node, (xmlChar*) "urgency");
	if (prop != NULL) {
		priv->urgency = as_urgency_kind_from_string (prop);
		g_free (prop);
	}

	for (iter = node->children; iter != NULL; iter = iter->next) {
		g_autofree gchar *content = NULL;
		if (iter->type != XML_ELEMENT_NODE)
			continue;

		if (g_strcmp0 ((gchar*) iter->name, "location") == 0) {
			content = as_xml_get_node_value (iter);
			as_release_add_location (release, content);
		} else if (g_strcmp0 ((gchar*) iter->name, "checksum") == 0) {
			g_autoptr(AsChecksum) cs = NULL;

			cs = as_checksum_new ();
			if (as_checksum_load_from_xml (cs, ctx, iter, NULL))
				as_release_add_checksum (release, cs);
		} else if (g_strcmp0 ((gchar*) iter->name, "size") == 0) {
			AsSizeKind s_kind;
			prop = (gchar*) xmlGetProp (iter, (xmlChar*) "type");

			s_kind = as_size_kind_from_string (prop);
			if (s_kind != AS_SIZE_KIND_UNKNOWN) {
				guint64 size;

				content = as_xml_get_node_value (iter);
				size = g_ascii_strtoull (content, NULL, 10);
				if (size > 0)
					as_release_set_size (release, size, s_kind);
			}
			g_free (prop);
		} else if (g_strcmp0 ((gchar*) iter->name, "description") == 0) {
			if (as_context_get_style (ctx) == AS_FORMAT_STYLE_COLLECTION) {
				g_autofree gchar *lang;

				/* for collection XML, the "description" tag has a language property, so parsing it is simple */
				content = as_xml_dump_node_children (iter);
				lang = as_xmldata_get_node_locale (ctx, iter);
				if (lang != NULL)
					as_release_set_description (release, content, lang);
			} else {
				as_xml_parse_metainfo_description_node (ctx,
									iter,
									(GHFunc) as_release_parse_xml_metainfo_description_cb,
									release);
			}
		}
	}

	return TRUE;
}

/**
 * as_release_to_xml_node:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @root: XML node to attach the new nodes to.
 *
 * Serializes the data to an XML node.
 **/
void
as_release_to_xml_node (AsRelease *release, AsContext *ctx, xmlNode *root)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	xmlNode *subnode;
	guint j;

	/* set release version */
	subnode = xmlNewChild (root, NULL, (xmlChar*) "release", (xmlChar*) "");
	xmlNewProp (subnode, (xmlChar*) "type",
		    (xmlChar*) as_release_kind_to_string (priv->kind));
	xmlNewProp (subnode, (xmlChar*) "version", (xmlChar*) priv->version);

	/* set release timestamp / date */
	if (priv->timestamp > 0) {
		g_autofree gchar *time_str = NULL;

		if (as_context_get_style (ctx) == AS_FORMAT_STYLE_COLLECTION) {
			time_str = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->timestamp);
			xmlNewProp (subnode, (xmlChar*) "timestamp",
					(xmlChar*) time_str);
		} else {
			GTimeVal time;
			time.tv_sec = priv->timestamp;
			time.tv_usec = 0;
			time_str = g_time_val_to_iso8601 (&time);
			xmlNewProp (subnode, (xmlChar*) "date",
					(xmlChar*) time_str);
		}
	}

	/* set release urgency, if we have one */
	if (priv->urgency != AS_URGENCY_KIND_UNKNOWN) {
		const gchar *urgency_str;
		urgency_str = as_urgency_kind_to_string (priv->urgency);
		xmlNewProp (subnode, (xmlChar*) "urgency",
				(xmlChar*) urgency_str);
	}

	/* add location urls */
	for (j = 0; j < priv->locations->len; j++) {
		const gchar *lurl = (const gchar*) g_ptr_array_index (priv->locations, j);
		xmlNewTextChild (subnode, NULL, (xmlChar*) "location", (xmlChar*) lurl);
	}

	/* add checksum node */
	for (j = 0; j < priv->checksums->len; j++) {
		AsChecksum *cs = AS_CHECKSUM (g_ptr_array_index (priv->checksums, j));
		as_checksum_to_xml_node (cs, ctx, subnode);
	}

	/* add size node */
	for (j = 0; j < AS_SIZE_KIND_LAST; j++) {
		if (as_release_get_size (release, j) > 0) {
			xmlNode *s_node;
			g_autofree gchar *size_str;

			size_str = g_strdup_printf ("%" G_GUINT64_FORMAT, as_release_get_size (release, j));
			s_node = xmlNewTextChild (subnode,
							NULL,
							(xmlChar*) "size",
							(xmlChar*) size_str);
			xmlNewProp (s_node,
					(xmlChar*) "type",
					(xmlChar*) as_size_kind_to_string (j));
		}
	}

	/* add description */
	as_xml_add_description_node (ctx, subnode, priv->description);
}

/**
 * as_release_load_from_yaml:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @node: the YAML node.
 * @error: a #GError.
 *
 * Loads data from a YAML field.
 **/
gboolean
as_release_load_from_yaml (AsRelease *release, AsContext *ctx, GNode *node, GError **error)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	GNode *n;

	/* propagate locale */
	as_release_set_context (release, ctx);

	for (n = node->children; n != NULL; n = n->next) {
		const gchar *key = as_yaml_node_get_key (n);
		const gchar *value = as_yaml_node_get_value (n);

		if (g_strcmp0 (key, "unix-timestamp") == 0) {
			priv->timestamp = atol (value);
		} else if (g_strcmp0 (key, "date") == 0) {
			g_autoptr(GDateTime) time;
			time = as_iso8601_to_datetime (value);
			if (time != NULL) {
				priv->timestamp = g_date_time_to_unix (time);
			} else {
				g_debug ("Invalid ISO-8601 date in %s", as_context_get_filename (ctx)); // FIXME: Better error, maybe with line number?
			}
		} else if (g_strcmp0 (key, "type") == 0) {
			priv->kind = as_release_kind_from_string (value);
		} else if (g_strcmp0 (key, "version") == 0) {
			as_release_set_version (release, value);
		} else if (g_strcmp0 (key, "urgency") == 0) {
			priv->urgency = as_urgency_kind_from_string (value);
		} else if (g_strcmp0 (key, "description") == 0) {
			g_autofree gchar *tmp = as_yaml_get_localized_value (ctx, n, NULL);
			as_release_set_description (release, tmp, NULL);
		} else {
			as_yaml_print_unknown ("release", key);
		}
	}

	return TRUE;
}

/**
 * as_release_emit_yaml:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @emitter: The YAML emitter to emit data on.
 *
 * Emit YAML data for this object.
 **/
void
as_release_emit_yaml (AsRelease *release, AsContext *ctx, yaml_emitter_t *emitter)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	guint j;

	/* start mapping for this release */
	as_yaml_mapping_start (emitter);

	/* version */
	as_yaml_emit_entry (emitter, "version", priv->version);

	/* type */
	as_yaml_emit_entry (emitter, "type", as_release_kind_to_string (priv->kind));

	/* timestamp & date */
	if (priv->timestamp > 0) {
		g_autofree gchar *time_str = NULL;

		if (as_context_get_style (ctx) == AS_FORMAT_STYLE_COLLECTION) {
			as_yaml_emit_entry_timestamp (emitter,
						      "unix-timestamp",
						      priv->timestamp);
		} else {
			GTimeVal time;
			time.tv_sec = priv->timestamp;
			time.tv_usec = 0;
			time_str = g_time_val_to_iso8601 (&time);
			as_yaml_emit_entry (emitter, "date", time_str);
		}
	}

	/* urgency */
	if (priv->urgency != AS_URGENCY_KIND_UNKNOWN) {
		as_yaml_emit_entry (emitter,
				    "urgency",
				    as_urgency_kind_to_string (priv->urgency));
	}

	/* description */
	as_yaml_emit_long_localized_entry (emitter,
					   "description",
					   priv->description);

	/* location URLs */
	if (priv->locations->len > 0) {
		as_yaml_emit_scalar (emitter, "locations");
		as_yaml_sequence_start (emitter);
		for (j = 0; j < priv->locations->len; j++) {
			const gchar *lurl = (const gchar*) g_ptr_array_index (priv->locations, j);
			as_yaml_emit_scalar (emitter, lurl);
		}

		as_yaml_sequence_end (emitter);
	}

	/* TODO: Checksum and size are missing, because they are not specified yet for DEP-11.
	 * Will need to be added when/if we need it. */

	/* end mapping for the release */
	as_yaml_mapping_end (emitter);
}

/**
 * as_release_to_variant:
 * @release: an #AsRelease
 * @builder: A #GVariantBuilder
 *
 * Serialize the current active state of this object to a GVariant
 * for use in the on-disk binary cache.
 */
void
as_release_to_variant (AsRelease *release, GVariantBuilder *builder)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	guint j;
	GVariantBuilder checksum_b;
	GVariantBuilder sizes_b;
	GVariantBuilder rel_b;

	GVariant *locations_var;
	GVariant *checksums_var;
	GVariant *sizes_var;
	gboolean have_sizes = FALSE;

	/* build checksum info */
	g_variant_builder_init (&checksum_b, G_VARIANT_TYPE_DICTIONARY);
	for (j = 0; j < priv->checksums->len; j++) {
		AsChecksum *cs = AS_CHECKSUM (g_ptr_array_index (priv->checksums, j));
		as_checksum_to_variant (cs, &checksum_b);
	}

	/* build size info */
	g_variant_builder_init (&sizes_b, G_VARIANT_TYPE_DICTIONARY);
	for (j = 0; j < AS_SIZE_KIND_LAST; j++) {
		if (as_release_get_size (release, (AsSizeKind) j) > 0) {
			g_variant_builder_add (&sizes_b, "{ut}",
						(AsSizeKind) j,
						as_release_get_size (release, (AsSizeKind) j));
			have_sizes = TRUE;
		}
	}

	g_variant_builder_init (&rel_b, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add_parsed (&rel_b, "{'kind', <%u>}", priv->kind);
	g_variant_builder_add_parsed (&rel_b, "{'version', %v}", as_variant_mstring_new (priv->version));
	g_variant_builder_add_parsed (&rel_b, "{'timestamp', <%t>}", priv->timestamp);
	g_variant_builder_add_parsed (&rel_b, "{'urgency', <%u>}", priv->urgency);
	g_variant_builder_add_parsed (&rel_b, "{'description', %v}", as_variant_mstring_new (as_release_get_description (release)));

	locations_var = as_variant_from_string_ptrarray (priv->locations);
	if (locations_var)
		g_variant_builder_add_parsed (&rel_b, "{'locations', %v}", locations_var);

	checksums_var = priv->checksums->len > 0? g_variant_builder_end (&checksum_b) : NULL;
	if (checksums_var)
		g_variant_builder_add_parsed (&rel_b, "{'checksums', %v}", checksums_var);

	sizes_var = have_sizes? g_variant_builder_end (&sizes_b) : NULL;
	if (sizes_var)
		g_variant_builder_add_parsed (&rel_b, "{'sizes', %v}", sizes_var);

	g_variant_builder_add_value (builder, g_variant_builder_end (&rel_b));
}

/**
 * as_release_set_from_variant:
 * @release: an #AsRelease
 * @variant: The #GVariant to read from.
 *
 * Read the active state of this object from a #GVariant serialization.
 * This is used by the on-disk binary cache.
 */
gboolean
as_release_set_from_variant (AsRelease *release, GVariant *variant, const gchar *locale)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	GVariant *tmp;
	GVariantDict rdict;
	GVariantIter riter;
	GVariant *inner_child;;

	as_release_set_active_locale (release, locale);
	g_variant_dict_init (&rdict, variant);

	priv->kind = as_variant_get_dict_uint32 (&rdict, "kind");

	as_release_set_version (release, as_variant_get_dict_mstr (&rdict, "version", &tmp));
	g_variant_unref (tmp);

	tmp = g_variant_dict_lookup_value (&rdict, "timestamp", G_VARIANT_TYPE_UINT64);
	priv->timestamp = g_variant_get_uint64 (tmp);
	g_variant_unref (tmp);

	priv->urgency = as_variant_get_dict_uint32 (&rdict, "urgency");

	as_release_set_description (release,
					as_variant_get_dict_mstr (&rdict, "description", &tmp),
					locale);
	g_variant_unref (tmp);

	/* locations */
	as_variant_to_string_ptrarray_by_dict (&rdict,
						"locations",
						priv->locations);

	/* sizes */
	tmp = g_variant_dict_lookup_value (&rdict, "sizes", G_VARIANT_TYPE_DICTIONARY);
	if (tmp != NULL) {
		g_variant_iter_init (&riter, tmp);
		while ((inner_child = g_variant_iter_next_value (&riter))) {
			AsSizeKind kind;
			guint64 size;

			g_variant_get (inner_child, "{ut}", &kind, &size);
			as_release_set_size (release, size, kind);

			g_variant_unref (inner_child);
		}
		g_variant_unref (tmp);
	}

	/* checksums */
	tmp = g_variant_dict_lookup_value (&rdict, "checksums", G_VARIANT_TYPE_DICTIONARY);
	if (tmp != NULL) {
		g_variant_iter_init (&riter, tmp);
		while ((inner_child = g_variant_iter_next_value (&riter))) {
			g_autoptr(AsChecksum) cs = as_checksum_new ();
			if (as_checksum_set_from_variant (cs, inner_child))
				as_release_add_checksum (release, cs);

			g_variant_unref (inner_child);
		}
		g_variant_unref (tmp);
	}

	return TRUE;
}

/**
 * as_release_new:
 *
 * Creates a new #AsRelease.
 *
 * Returns: (transfer full): a #AsRelease
 **/
AsRelease*
as_release_new (void)
{
	AsRelease *release;
	release = g_object_new (AS_TYPE_RELEASE, NULL);
	return AS_RELEASE (release);
}
