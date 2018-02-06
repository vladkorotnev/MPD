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

namespace Dms {

struct Action
{
	enum {
		UP 		= 1,
		DOWN	= 2,
		NEXT	= 4,
		PREVIOUS= 8,
	};

	unsigned action;

	Action() : action(0) {}

	static std::string validArgs(unsigned a);

	bool parse(const char *s, unsigned a);

	/**
	 * amend c by action, and by [start,end); circle loop
	 */
	unsigned amend(unsigned c, unsigned start, unsigned end) const;

	/**
	 * amend c by action, and by [min,max], not circle loop, notic here!!
	 */
	double amend(double c, double min, double max, double step) const;

	/**
	 * amend c by action, and by [min,max], not circle loop, notic here!!
	 */
	unsigned char amend(unsigned char c, unsigned char min, unsigned char max, unsigned char step) const;
};

}

