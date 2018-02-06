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
 
#ifndef MPD_LINE_PARSER_HXX
#define MPD_LINE_PARSER_HXX

#include "Compiler.h"
#include <stddef.h>
#include <stdint.h>

class LineParser {
public:
	LineParser(const char *line = nullptr, const char *needle = ":=");

	void clear();

	bool feed(const char *line);
	
	inline bool hasName() {
		return name != nullptr && name[0] != '\0' && name_size > 0;
	}

	inline bool hasValue() {
		return value != nullptr && value[0] != '\0';
	}

	bool parseName(const char *n, bool case_sensitivity = true);

	bool toBool(bool *ok = nullptr, bool def_value = false);

	int toInt(bool *ok = nullptr, int def_value = 0);

	unsigned toUnsigned(bool *ok = nullptr, unsigned def_value = 0);

	int64_t toInt64(bool *ok = nullptr, int64_t def_value = 0);

	uint64_t toUint64(bool *ok = nullptr, uint64_t def_value = 0);

	float toFloat(bool *ok = nullptr, float def_value = 0.0);

	double toDouble(bool *ok = nullptr, double def_value = 0.0);

	const char *name;

	size_t name_size;

	const char *value;

	const char *needle;
};

#endif
