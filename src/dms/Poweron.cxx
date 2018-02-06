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
#include "Poweron.hxx"
#include "util/ASCII.hxx"
#include "util/Macros.hxx"
#include "util/Domain.hxx"
#include "protocol/Ack.hxx"
#include "Log.hxx"
#include "dms/util/IOReader.hxx"
#include "dms/util/IOWriter.hxx"

namespace Dms {

static constexpr Domain domain("Poweron");

static const char *poweron_tbl[] = {
	"off",
	"on",
	"startup",
	"ok",
	"offing",
};

const char *
Poweron::c_str() const
{
	assert(poweron < ARRAY_SIZE(poweron_tbl));

	return poweron_tbl[poweron];
}

std::string
Poweron::validArgs()
{
	std::string str;

	for (const char *s : poweron_tbl) {
		str.append(s).append(" ");
	}

	return str;
}

bool
Poweron::parse(const char *s)
{
	for (unsigned i=0;i<ARRAY_SIZE(poweron_tbl);i++) {
		if (StringEqualsCaseASCII(s, poweron_tbl[i])) {
			poweron = i;
			return true;
		}
	}

	throw FormatProtocolError(ACK_ERROR_ARG, "%s expected:%s", validArgs().c_str(), s);
}

bool
Poweron::parse(unsigned char p)
{
	if (p < ARRAY_SIZE(poweron_tbl)) {
		poweron = p;
		return true;
	}

	return false;
}

#define FILE_POWERON	"/sys/dms-m3/poweron"

void
Poweron::apply() const
{
	IOWriter writer(FILE_POWERON);
	unsigned char o = poweron;
	writer.write((void*)&o, sizeof(o));
}

void
Poweron::acquire()
{
	IOReader reader(FILE_POWERON);
	unsigned char o;
	if (reader.read((void*)&o, sizeof(o)) != sizeof(o)) {
		throw;
	}
	if (o >= 0x30) {
		o -= 0x30;
	}

	assert(o < ARRAY_SIZE(poweron_tbl));

	poweron = o;
}

}
