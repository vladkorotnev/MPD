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
#include "Action.hxx"
#include "util/ASCII.hxx"
#include "util/StringUtil.hxx"

namespace Dms {

std::string
Action::validArgs(unsigned a)
{
	std::string str;

	if (a & UP) {
		str.append("up");
	}
	if (a & DOWN) {
		if (!str.empty())
			str.append(" ");
		str.append("down");
	}
	if (a & NEXT) {
		if (!str.empty())
			str.append(" ");
		str.append("next");
	}
	if (a & PREVIOUS) {
		if (!str.empty())
			str.append(" ");
		str.append("previous");
	}

	return str;
}

bool
Action::parse(const char *s, unsigned a)
{
	bool ret = true;

	if ((a & UP) &&
		(StringEqualsCaseASCII(s, "up") || StringStartsWith(s, "up_"))) {
		action = UP;
	} else if ((a & DOWN) &&
		(StringEqualsCaseASCII(s, "down") || StringStartsWith(s, "down_"))) {
		action = DOWN;
	} else if ((a & NEXT) && StringEqualsCaseASCII(s, "next")) {
		action = NEXT;
	} else if ((a & PREVIOUS) && StringEqualsCaseASCII(s, "previous")) {
		action = PREVIOUS;
	} else {
		action = 0;
		ret = false;
	}
	
	return ret;
}

unsigned
Action::amend(unsigned c, unsigned start, unsigned end) const
{
	assert(start < end);
	assert(end > 0);

	if (action & UP
		|| action & NEXT) {
		c = (c+1) >= end ? start : (c+1);
	} else if (action & DOWN
		|| action & PREVIOUS) {
		c = c <= start ? (end-1) : (c-1);
	}

	if (c < start) {
		c = start;
	} else if (c >= end) {
		c = end - 1;
	}

	return c;
}

double
Action::amend(double c, double min, double max, double step) const
{
	assert(min <= max);

	if (action & UP
		|| action & NEXT) {
		c = c >= max ? max : c + step;
	} else if (action & DOWN
		|| action & PREVIOUS) {
		c = c <= min ? min : c - step;
	}

	if (c < min) {
		c = min;
	} else if (c > max) {
		c = max;
	}

	return c;
}

unsigned char
Action::amend(unsigned char c, unsigned char min, unsigned char max, unsigned char step) const
{
	assert(min <= max);

	if (action & UP
		|| action & NEXT) {
		c = c >= max ? max : c + step;
	} else if (action & DOWN
		|| action & PREVIOUS) {
		c = c <= min ? min : c - step;
	}
	if (c < min) {
		c = min;
	} else if (c > max) {
		c = max;
	}

	return c;
}

}
