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
 
#include "LineParser.hxx"
#include "NumberParser.hxx"

#include <assert.h>
#include <string.h>

LineParser::LineParser(const char *line, const char *_needle)
{
	needle = _needle;
	feed(line);
}

void
LineParser::clear()
{
	name = nullptr;
	name_size = 0;
	value = nullptr;
}

bool
LineParser::feed(const char *line)
{
	clear();
	if (line == nullptr) {
		return false;
	}
	// strip name left
	name = line + strspn(line, " \t\n");
	const char *p = strpbrk(name, needle);
	if (p == nullptr) {
		name_size = strlen(name);
		return false;
	}
	if (strchr(needle, p[1]) == 0) {
		value = p + 2;
	} else {
		value = p + 1;
	}

	// strip name right
	while (p > name && strchr(needle, p[0]) == 0) {
		p--;
	}
	name_size = p - name;

	return true;
}

bool
LineParser::parseName(const char *n, bool case_sensitivity)
{
	if (!hasName()) {
		return false;
	}

	return case_sensitivity ? strncmp(name, n, name_size) == 0 :
		strncasecmp(name, n, name_size) == 0;
}

bool
LineParser::toBool(bool *ok, bool def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	bool ret = ParseBool(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

int
LineParser::toInt(bool *ok, int def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	int ret = ParseInt(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

unsigned
LineParser::toUnsigned(bool *ok, unsigned def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	unsigned ret = ParseUnsigned(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

int64_t
LineParser::toInt64(bool *ok, int64_t def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	int64_t ret = ParseInt64(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

uint64_t
LineParser::toUint64(bool *ok, uint64_t def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	uint64_t ret = ParseUint64(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

float
LineParser::toFloat(bool *ok, float def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	float ret = ParseFloat(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

double
LineParser::toDouble(bool *ok, double def_value)
{
	if (!hasValue()) {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}
	char *endptr = nullptr;
	double ret = ParseDouble(value, &endptr);
	if (endptr == value || *endptr != '\0') {
		if (ok != nullptr)
			*ok = false;
		return def_value;
	}

	if (ok != nullptr)
		*ok = true;
	return ret;
}

