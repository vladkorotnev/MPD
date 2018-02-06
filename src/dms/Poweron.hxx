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

struct Poweron
{
	enum { OFF, ON, STARTUP, OK, OFFING };

	unsigned char	poweron;

	Poweron(unsigned o = STARTUP)
		: poweron(o) {}

	const char *c_str() const;

	gcc_pure
	static std::string validArgs();

	bool parse(const char *s);

	bool parse(unsigned char p);

	bool isOff() const {
		return poweron == OFF;
	}

	bool isOn() const {
		return poweron == ON;
	}

	bool isStartup() const {
		return poweron == STARTUP;
	}

	bool isOk() const {
		return poweron == OK;
	}

	bool isOffing() const {
		return poweron == OFFING;
	}

	bool isRunning() const {
		return (poweron == ON) || (poweron == STARTUP) || (poweron == OK);
	}

	bool isPoweron() const {
		return (poweron == ON) || (poweron == STARTUP);
	}

	void apply() const;

	void acquire();
};

}

