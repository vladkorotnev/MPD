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
#include "dms/Database.hxx"
#include "sticker/StickerDatabase.hxx"
#include "util/NumberParser.hxx"
#include "util/Domain.hxx"
#include "util/Error.hxx"
#include "Log.hxx"

#include <stdio.h>

namespace Dms {

static constexpr Domain domain("DmsDatabase");


static std::string
load_value(const char *type, const char *uri, const char *name)
{
	return sticker_load_value(type, uri, name, IgnoreError());
}

static bool
store_value(const char *type, const char *uri, const char *name, const char *value)
{
	return sticker_store_value(type, uri, name, value, IgnoreError());
}

static Sticker *
list_value(const char *type, const char *uri)
{
	return sticker_load(type, uri, IgnoreError());
}

bool
DmsDatabase::load(const char *name, bool defValue) const
{
	assert(name != nullptr);

	std::string str = load_value(type.c_str(), uri.c_str(), name);
	if (str.empty()) {
		return defValue;
	}

	char *endptr;
	auto val = ParseBool(str.c_str(), &endptr);
	if (*endptr != '\0') {
		return defValue;
	}

	return val;
}

int
DmsDatabase::load(const char *name, int defValue) const
{
	assert(name != nullptr);

	std::string str = load_value(type.c_str(), uri.c_str(), name);
	if (str.empty()) {
		return defValue;
	}

	char *endptr;
	auto val = ParseInt(str.c_str(), &endptr);
	if (*endptr != '\0') {
		return defValue;
	}

	return val;
}

unsigned
DmsDatabase::load(const char *name, unsigned defValue) const
{
	assert(name != nullptr);

	std::string str = load_value(type.c_str(), uri.c_str(), name);
	if (str.empty()) {
		return defValue;
	}

	char *endptr;
	auto val = ParseUnsigned(str.c_str(), &endptr);
	if (*endptr != '\0') {
		return defValue;
	}

	return val;
}

std::string
DmsDatabase::load(const char *name, const char *defValue) const
{
	assert(name != nullptr);

	std::string str = load_value(type.c_str(), uri.c_str(), name);
	if (str.empty()) {
		return defValue;
	}

	return str;
}

bool
DmsDatabase::store(const char *name, bool value) const
{
	assert(name != nullptr);

	char buf[20] = {0};
	buf[0] = value ? '1' : '0';
	return store_value(type.c_str(), uri.c_str(), name, buf);
}

bool
DmsDatabase::store(const char *name, int value) const
{
	assert(name != nullptr);

	char buf[20] = {0};
	snprintf(buf, sizeof(buf), "%d", value);
	return store_value(type.c_str(), uri.c_str(), name, buf);
}

bool
DmsDatabase::store(const char *name, unsigned value) const
{
	assert(name != nullptr);

	char buf[20] = {0};
	snprintf(buf, sizeof(buf), "%u", value);
	return store_value(type.c_str(), uri.c_str(), name, buf);
}

bool
DmsDatabase::store(const char *name, const char *value) const
{
	assert(name != nullptr);

	return store_value(type.c_str(), uri.c_str(), name, value);
}

Sticker *
DmsDatabase::list() const
{
	return list_value(type.c_str(), uri.c_str());
}

}
