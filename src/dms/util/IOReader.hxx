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

#include <string>

class FileReader;
class BufferedReader;

namespace Dms {

/* base one line */
class IOReader {
	FileReader *const file_reader;

	BufferedReader *const buffered_reader;

public:
	/* throw error */
	IOReader(const char *filename);

	IOReader(const IOReader &other) = delete;

	~IOReader();

	char *readline();

	bool read(bool defValue = false);

	int read(int defValue = 0);

	unsigned read(unsigned defValue = 0);

	std::string read(std::string defValue = std::string());

	std::string read(const char *defValue = "") {
		return read(std::string(defValue));
	}

	size_t read(void *data, size_t len);

	void readFull(void *data, size_t len);
};

}
