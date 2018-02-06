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

#include "check.h"
#include "Compiler.h"
#include "system/FileDescriptor.hxx"

#include <string>

namespace Dms {

class IOWriter {
	FileDescriptor fd;

public:
	/* throw error */
	IOWriter(const char *filename, bool creat = false);

	IOWriter(const IOWriter &other) = delete;

	~IOWriter();

	void write(bool val);

	void write(int val);

	void write(unsigned val);

	void write(const char *val);

	void write(const void *data, size_t len);
};

class IOWriterNoException {
	IOWriter *writer;

public:
	IOWriterNoException(const char *filename, bool creat = false);

	IOWriterNoException(const IOWriterNoException &other) = delete;

	~IOWriterNoException();

	bool isDefined() const {
		return writer != nullptr;
	}

	bool write(bool val);

	bool write(int val);

	bool write(unsigned val);

	bool write(const char *val);

	bool write(const void *data, size_t len);
};

}

