/*
 * Copyright (C) 2003-2017 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
 
#include "config.h"
#include "Sticker.hxx"
#include "sticker/StickerDatabase.hxx"
#include "util/Alloc.hxx"
#include "util/Error.hxx"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

namespace Dms {

static const char *type = "misc";

std::string
dms_load_value(const char *uri, const char *name)
{
	return sticker_load_value(type, uri, name, IgnoreError());
}

void
dms_store_value(const char *uri,
		       const char *name, const char *value)
{
	sticker_store_value(type, uri, name, value, IgnoreError());
}

bool
dms_delete(const char *uri)
{
	return sticker_delete(type, uri, IgnoreError());
}

bool
dms_delete_value(const char *uri, const char *name)
{
	return sticker_delete_value(type, uri, name, IgnoreError());
}

Sticker *
dms_load(const char *uri)
{
	return sticker_load(type, uri, IgnoreError());
}

}
