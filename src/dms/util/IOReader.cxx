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
#include "IOReader.hxx"
#include "util/NumberParser.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/io/FileReader.hxx"
#include "fs/io/BufferedReader.hxx"

#include <stdexcept>

namespace Dms {

IOReader::IOReader(const char *filename)
	:file_reader(new FileReader(AllocatedPath::FromUTF8(filename),IgnoreError())),
	buffered_reader(new BufferedReader(*file_reader))
{

}

IOReader::~IOReader()
{
	delete buffered_reader;
	delete file_reader;
}

char *
IOReader::readline()
{
	assert(buffered_reader != nullptr);

	if (!file_reader->IsDefined() ||
		!buffered_reader->Check()) {
		return nullptr;
	}

	return buffered_reader->ReadLine();
}


bool
IOReader::read(bool defValue)
{
	const char *line = readline();

	if (line == nullptr
		|| *line == '\0') {
		return defValue;
	}

	char *endptr;
	auto val = ParseBool(line, &endptr);
	if (*endptr != '\0') {
		return defValue;
	}

	return val;
}

int
IOReader::read(int defValue)
{
	const char *line = readline();

	if (line == nullptr
		|| *line == '\0') {
		return defValue;
	}

	char *endptr;
	auto val = ParseInt(line, &endptr);
	if (*endptr != '\0') {
		return defValue;
	}

	return val;
}

unsigned
IOReader::read(unsigned defValue)
{
	const char *line = readline();

	if (line == nullptr
		|| *line == '\0') {
		return defValue;
	}

	char *endptr;
	auto val = ParseUnsigned(line, &endptr);
	if (*endptr != '\0') {
		return defValue;
	}

	return val;
}

std::string
IOReader::read(std::string defValue)
{
	const char *line = readline();

	if (line == nullptr) {
		return defValue;
	}

	return std::string(line);
}

size_t
IOReader::read(void *data, size_t len)
{
	assert(buffered_reader != nullptr);

	size_t total = 0;
	char *pdata = (char*)data;

	do {
		auto r = buffered_reader->Read();
		size_t min = std::min<size_t>(len, r.size);
		memcpy(pdata, r.data, min);
		len -= min;
		total += min;
		pdata += min;
		buffered_reader->Consume(min);
		if (len == 0) {
			return total;
		}
	} while (buffered_reader->Fill(true));

	return total;
}

void
IOReader::readFull(void *data, size_t len)
{
	if (read(data, len) != len) {
		throw std::runtime_error("read too short");
	}
}

}
