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
#include "Playmode.hxx"
#include "Sticker.hxx"
#include "util/ASCII.hxx"
#include "util/Macros.hxx"
#include "Partition.hxx"

#include <assert.h>

namespace Dms {

static const char *Playmode_tbl[] = {
	"sequence",
	"shuffle",
	"single",
	"all",
};

const char *
Playmode::c_str() const
{
	assert(mode < ARRAY_SIZE(Playmode_tbl));

	return Playmode_tbl[mode];
}

std::string
Playmode::validArgs() const
{
	std::string str;

	for (const char *s : Playmode_tbl) {
		str.append(s).append(" ");
	}
	str.append(Action::validArgs(Action::NEXT | Action::PREVIOUS));

	return str;
}

bool
Playmode::parse(const char *s, bool enable_action, const char *uri)
{
	if (enable_action
		&& Action::parse(s, Action::NEXT | Action::PREVIOUS)) {
		load(uri);
		mode = Action::amend(mode, 0, ARRAY_SIZE(Playmode_tbl));
		return true;
	}

	for (unsigned i=0;i<ARRAY_SIZE(Playmode_tbl);i++) {
		if (StringEqualsCaseASCII(s, Playmode_tbl[i])) {
			mode = i;
			return true;
		}
	}

	return false;
}

void
Playmode::load(const char *uri)
{
	std::string str = dms_load_value(uri, "playmode");

	assert(str.compare("next") != 0);

	parse(str.c_str(), false);
}

void
Playmode::store(const char *uri) const
{
	dms_store_value(uri, "playmode", c_str());
}

void
Playmode::apply(Partition &p) const
{
	bool repeat, random, single;

	switch (mode) {
	case SEQUENCE:
		repeat = false;
		random = false;
		single = false;
		break;
	case ALL:
		repeat = true;
		random = false;
		single = false;
		break;
	case SHUFFLE:
		repeat = true;
		random = true;
		single = false;
		break;
	case SINGLE:
		repeat = true;
		random = false;
		single = true;
		break;
	default:
		assert(false);
		gcc_unreachable();
	}
	p.SetRepeat(repeat);
	p.SetRandom(random);
	p.SetSingle(single);
}

}
