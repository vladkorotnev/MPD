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
 
#ifndef MPD_MISC_STICKER_HXX
#define MPD_MISC_STICKER_HXX

#include "Match.hxx"
#include "Compiler.h"

#include <string>

struct Sticker;
class Error;

std::string
sticker_misc_get_value(const char *uri, const char *name, Error &error);

bool
sticker_misc_set_value(const char *uri,
		       const char *name, const char *value,
		       Error &error);

bool
sticker_misc_delete(const char *uri, Error &error);

bool
sticker_misc_delete_value(const char *uri, const char *name,
			  Error &error);

Sticker *
sticker_misc_get(const char *uri, Error &error);

#endif
