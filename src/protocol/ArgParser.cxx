/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
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
#include "ArgParser.hxx"
#include "Result.hxx"
#include "Ack.hxx"
#include "Chrono.hxx"

#include <limits>

#include <stdlib.h>
#include <string.h>
#include "Log.hxx"
#include "util/Domain.hxx"

static constexpr Domain dms_domain("dms_command");

bool
check_uint32(Client &client, uint32_t *dst, const char *s)
{
	char *test;

	*dst = strtoul(s, &test, 10);
	if (test == s || *test != '\0') {
		command_error(client, ACK_ERROR_ARG,
			      "Integer expected: %s", s);
		return false;
	}
	return true;
}

bool
check_int(Client &client, int *value_r, const char *s)
{
	char *test;
	long value;

	value = strtol(s, &test, 10);
	if (test == s || *test != '\0') {
		command_error(client, ACK_ERROR_ARG,
			      "Integer expected: %s", s);
		return false;
	}

	if (value < std::numeric_limits<int>::min() ||
	    value > std::numeric_limits<int>::max()) {
		command_error(client, ACK_ERROR_ARG,
			      "Number too large: %s", s);
		return false;
	}

	*value_r = (int)value;
	return true;
}

bool
check_range(Client &client, unsigned *value_r1, unsigned *value_r2,
	    const char *s)
{
	char *test, *test2;
	long value;

	value = strtol(s, &test, 10);
	if (test == s || (*test != '\0' && *test != ':')) {
		command_error(client, ACK_ERROR_ARG,
			      "Integer or range expected: %s", s);
		return false;
	}

	if (value == -1 && *test == 0) {
		/* compatibility with older MPD versions: specifying
		   "-1" makes MPD display the whole list */
		*value_r1 = 0;
		*value_r2 = std::numeric_limits<int>::max();
		return true;
	}

	if (value < 0) {
		command_error(client, ACK_ERROR_ARG,
			      "Number is negative: %s", s);
		return false;
	}

	if (unsigned(value) > std::numeric_limits<unsigned>::max()) {
		command_error(client, ACK_ERROR_ARG,
			      "Number too large: %s", s);
		return false;
	}

	*value_r1 = (unsigned)value;

	if (*test == ':') {
		value = strtol(++test, &test2, 10);
		if (*test2 != '\0') {
			command_error(client, ACK_ERROR_ARG,
				      "Integer or range expected: %s", s);
			return false;
		}

		if (test == test2)
			value = std::numeric_limits<int>::max();

		if (value < 0) {
			command_error(client, ACK_ERROR_ARG,
				      "Number is negative: %s", s);
			return false;
		}

		if (unsigned(value) > std::numeric_limits<unsigned>::max()) {
			command_error(client, ACK_ERROR_ARG,
				      "Number too large: %s", s);
			return false;
		}

		*value_r2 = (unsigned)value;
		if (*value_r2 < *value_r1) {
			command_error(client, ACK_ERROR_ARG,
				      "%u must large than %u", *value_r2, *value_r1);
			return false;
		}
	} else {
		*value_r2 = (unsigned)value + 1;
	}

	return true;
}

bool
check_unsigned(Client &client, unsigned *value_r, const char *s)
{
	unsigned long value;
	char *endptr;

	value = strtoul(s, &endptr, 10);
	if (endptr == s || *endptr != 0) {
		command_error(client, ACK_ERROR_ARG,
			      "Integer expected: %s", s);
		return false;
	}

	if (value > std::numeric_limits<unsigned>::max()) {
		command_error(client, ACK_ERROR_ARG,
			      "Number too large: %s", s);
		return false;
	}

	*value_r = (unsigned)value;
	return true;
}

bool
check_bool(Client &client, bool *value_r, const char *s)
{
	long value;
	char *endptr;

	if (strcasecmp(s, "on") == 0
		|| strcasecmp(s, "enable") == 0
		|| strcasecmp(s, "yes") == 0
		|| strcasecmp(s, "true") == 0) {
		*value_r = true;
		return true;
	}
	if (strcasecmp(s, "off") == 0
		|| strcasecmp(s, "disable") == 0
		|| strcasecmp(s, "no") == 0
		|| strcasecmp(s, "false") == 0) {
		*value_r = false;
		return true;
	}

	value = strtol(s, &endptr, 10);
	if (endptr == s || *endptr != 0 || (value != 0 && value != 1)) {
		command_error(client, ACK_ERROR_ARG,
			      "Boolean (0/1) expected: %s", s);
		return false;
	}

	*value_r = !!value;
	return true;
}

bool
check_float(Client &client, float *value_r, const char *s)
{
	float value;
	char *endptr;

	value = strtof(s, &endptr);
	if (endptr == s || *endptr != 0) {
		command_error(client, ACK_ERROR_ARG,
			      "Float expected: %s", s);
		return false;
	}

	*value_r = value;
	return true;
}

bool
check_double(Client &client, double *value_r, const char *s)
{
	double value;
	char *endptr;

	value = strtod(s, &endptr);
	if (endptr == s || *endptr != 0) {
		command_error(client, ACK_ERROR_ARG,
			      "Double expected: %s", s);
		return false;
	}

	*value_r = value;
	return true;
}

bool
check_hex(gcc_unused Client &client, const char *s)
{
	int length = strlen(s);
	int i;

	for (i=0;i<length;i++) {
		if (!((s[i] >= '0' && s[i] <= '9')
			|| (s[i] >= 'a' && s[i] <= 'f'))) {
			FormatDefault(dms_domain, "%s %d %c", __func__, __LINE__, s[i]);
			return false;
		}
	}
	return true;
}

bool
ParseCommandArg(Client &client, SongTime &value_r, const char *s)
{
	float value;
	bool success = check_float(client, &value, s) && value >= 0;
	if (success)
		value_r = SongTime::FromS(value);

	return success;
}

bool
ParseCommandArg(Client &client, SignedSongTime &value_r, const char *s)
{
	float value;
	bool success = check_float(client, &value, s);
	if (success)
		value_r = SignedSongTime::FromS(value);

	return success;
}


uint32_t
ParseCommandArgU32(const char *s)
{
	char *test;
	auto value = strtoul(s, &test, 10);
	if (test == s || *test != '\0')
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Integer expected: %s", s);

	return value;
}

int
ParseCommandArgInt(const char *s, int min_value, int max_value)
{
	char *test;
	auto value = strtol(s, &test, 10);
	if (test == s || *test != '\0')
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Integer expected: %s", s);

	if (value < min_value || value > max_value)
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Number too large: %s", s);

	return (int)value;
}

int
ParseCommandArgInt(const char *s)
{
	return ParseCommandArgInt(s,
				  std::numeric_limits<int>::min(),
				  std::numeric_limits<int>::max());
}

RangeArg
ParseCommandArgRange(const char *s)
{
	char *test, *test2;
	auto value = strtol(s, &test, 10);
	if (test == s || (*test != '\0' && *test != ':'))
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Integer or range expected: %s", s);

	if (value == -1 && *test == 0)
		/* compatibility with older MPD versions: specifying
		   "-1" makes MPD display the whole list */
		return RangeArg::All();

	if (value < 0)
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Number is negative: %s", s);

	if (value > std::numeric_limits<int>::max())
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Number too large: %s", s);

	RangeArg range;
	range.start = (unsigned)value;

	if (*test == ':') {
		value = strtol(++test, &test2, 10);
		if (*test2 != '\0')
			throw FormatProtocolError(ACK_ERROR_ARG,
						  "Integer or range expected: %s",
						  s);

		if (test == test2)
			value = std::numeric_limits<int>::max();

		if (value < 0)
			throw FormatProtocolError(ACK_ERROR_ARG,
						  "Number is negative: %s", s);


		if (value > std::numeric_limits<int>::max())
			throw FormatProtocolError(ACK_ERROR_ARG,
						  "Number too large: %s", s);

		range.end = (unsigned)value;
	} else {
		range.end = (unsigned)value + 1;
	}

	return range;
}

unsigned
ParseCommandArgUnsigned(const char *s, unsigned max_value)
{
	char *endptr;
	auto value = strtoul(s, &endptr, 10);
	if (endptr == s || *endptr != 0)
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Integer expected: %s", s);

	if (value > max_value)
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Number too large: %s", s);

	return (unsigned)value;
}

unsigned
ParseCommandArgUnsigned(const char *s)
{
	return ParseCommandArgUnsigned(s,
				       std::numeric_limits<unsigned>::max());
}

bool
ParseCommandArgBool(const char *s)
{
	char *endptr;
	auto value = strtol(s, &endptr, 10);
	if (endptr == s || *endptr != 0 || (value != 0 && value != 1))
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Boolean (0/1) expected: %s", s);

	return !!value;
}

float
ParseCommandArgFloat(const char *s)
{
	char *endptr;
	auto value = strtof(s, &endptr);
	if (endptr == s || *endptr != 0)
		throw FormatProtocolError(ACK_ERROR_ARG,
					  "Float expected: %s", s);

	return value;
}

SongTime
ParseCommandArgSongTime(const char *s)
{
	auto value = ParseCommandArgFloat(s);
	return SongTime::FromS(value);
}

SignedSongTime
ParseCommandArgSignedSongTime(const char *s)
{
	auto value = ParseCommandArgFloat(s);
	return SongTime::FromS(value);
}