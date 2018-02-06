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
 
#pragma once

#include "Compiler.h"

#include <string>

struct Sticker;

namespace Dms {

class DmsDatabase
{
	std::string type;
	std::string uri;

public:
	DmsDatabase(const char *_type, const char *_uri)
		: type(_type),
		uri(_uri) {
	}

	bool load(const char *name, bool defValue = true) const;

	int load(const char *name, int defValue = 0) const;

	unsigned load(const char *name, unsigned defValue = 0) const;

	std::string load(const char *name, std::string defValue = "") const {
		return load(name, defValue.c_str());
	}

	std::string load(const char *name, const char *defValue = "") const;

	bool store(const char *name, bool value) const;

	bool store(const char *name, int value) const;

	bool store(const char *name, unsigned value) const;

	bool store(const char *name, const std::string &value) const {
		return store(name, value.c_str());
	}

	bool store(const char *name, const char *value) const;

	Sticker *list() const;
};

}
