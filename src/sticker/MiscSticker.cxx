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
#include "MiscSticker.hxx"
#include "StickerDatabase.hxx"
#include "util/Error.hxx"
#include "util/Alloc.hxx"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

std::string
sticker_misc_get_value(const char *uri, const char *name, Error &error)
{
	return sticker_load_value("misc", uri, name, error);
}

bool
sticker_misc_set_value(const char *uri,
		       const char *name, const char *value,
		       Error &error)
{
	return sticker_store_value("misc", uri, name, value, error);
}

bool
sticker_misc_delete(const char *uri, Error &error)
{
	return sticker_delete("misc", uri, error);
}

bool
sticker_misc_delete_value(const char *uri, const char *name,
			  Error &error)
{
	return sticker_delete_value("misc", uri, name, error);
}

Sticker *
sticker_misc_get(const char *uri, Error &error)
{
	return sticker_load("misc", uri, error);
}

