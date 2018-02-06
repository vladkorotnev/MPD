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
#include "IOWriter.hxx"
#include "system/Error.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

namespace Dms {

IOWriter::IOWriter(const char *filename, bool creat)
{
	assert(filename != nullptr);

	if (!fd.Open(filename, O_WRONLY | (creat ? (O_CREAT|O_TRUNC):0))) {
		throw FormatErrno("Failed to open %s", filename);
	}
}

IOWriter::~IOWriter()
{
	if (fd.IsDefined()) {
		fd.Close();
	}
}

void
IOWriter::write(bool val)
{
	fd.Write((val ? "1" : "0"), 1);
}

void
IOWriter::write(int val)
{
	char buf[20];

	fd.Write(buf, snprintf(buf, sizeof(buf), "%d", val));
}

void
IOWriter::write(unsigned val)
{
	char buf[20];

	fd.Write(buf, snprintf(buf, sizeof(buf), "%u", val));
}

void
IOWriter::write(const char *val)
{
	assert(val != nullptr);

	fd.Write(val, strlen(val));
}

void
IOWriter::write(const void *data, size_t len)
{
	assert(data != nullptr);

	fd.Write(data, len);
}

IOWriterNoException::IOWriterNoException(const char *filename, bool creat)
try {
	writer = new IOWriter(filename, creat);
} catch (...) {
	writer = nullptr;
}

IOWriterNoException::~IOWriterNoException()
{
	delete writer;
	writer = nullptr;
}

bool
IOWriterNoException::write(bool val)
try {
	assert(writer != nullptr);

	writer->write(val);
	return true;
} catch (...) {
	return false;
}

bool
IOWriterNoException::write(int val)
try {
	assert(writer != nullptr);

	writer->write(val);
	return true;
} catch (...) {
	return false;
}

bool
IOWriterNoException::write(unsigned val)
try {
	assert(writer != nullptr);

	writer->write(val);
	return true;
} catch (...) {
	return false;
}

bool
IOWriterNoException::write(const char *val)
try {
	assert(writer != nullptr);
	assert(val != nullptr);

	writer->write(val);
	return true;
} catch (...) {
	return false;
}

bool
IOWriterNoException::write(const void *data, size_t len)
try {
	assert(writer != nullptr);
	assert(data != nullptr);

	writer->write(data, len);
	return true;
} catch (...) {
	return false;
}

}
