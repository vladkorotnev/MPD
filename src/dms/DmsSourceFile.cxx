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
#include "DmsSourceFile.hxx"
#include "fs/io/TextFile.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/FileSystem.hxx"
#include "util/Domain.hxx"
#include "util/StringUtil.hxx"
#include "Log.hxx"
#include "DmsArgs.hxx"

#include <string.h>

static constexpr Domain domain("dms_source_file");

#define DMS_SOURCE_FILE_PATH			"/data/mpd/dms_source"

#define SOURCE			"source: "
#define SOURCE_NAME		"source_name: "

static inline bool
Write(OutputStream &os, Error &error)
{
	BufferedOutputStream bos(os);
	dms_source_file_write(bos);
	return bos.Flush(error);
}

void
dms_source_file_save()
{
	FormatDefault(domain,
		    "Saving dms source file %s", DMS_SOURCE_FILE_PATH);

	Error error;
	AllocatedPath path_fs = AllocatedPath::FromUTF8(DMS_SOURCE_FILE_PATH, error);
	FileOutputStream fos(path_fs, error);
	if (!fos.IsDefined() || !Write(fos, error) || !fos.Commit(error)) {
		LogError(error);
		return;
	}
}

void
dms_source_file_restore()
{
	FormatDebug(domain, "Loading dms source file %s", DMS_SOURCE_FILE_PATH);

	Error error;
	AllocatedPath path_fs = AllocatedPath::FromUTF8(DMS_SOURCE_FILE_PATH, error);
	TextFile file(path_fs, error);
	if (file.HasFailed()) {
		LogError(error);
		return;
	}

	const char *line;
	DmsSource source;
	while ((line = file.ReadLine()) != nullptr) {
		if (StringStartsWith(line, SOURCE)) {
			source.parse(line + strlen(SOURCE));
		} else if (StringStartsWith(line, SOURCE_NAME)) {
			renameSourceName(source, line + strlen(SOURCE_NAME));
		}
	}
}

