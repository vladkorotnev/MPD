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
#include "Action.hxx"

#include <string>

struct Partition;

namespace Dms {

struct Playmode: Action
{
	enum { SEQUENCE, SHUFFLE, SINGLE, ALL };

	unsigned	mode;

	Playmode(unsigned m=SEQUENCE):mode(m) {}

	const char *c_str() const;

	std::string validArgs() const;

	bool parse(const char *s, bool enable_action=true, const char *uri="dms");

	void load(const char *uri="dms");

	void store(const char *uri="dms") const;

	void apply(Partition &p) const;
};

}
