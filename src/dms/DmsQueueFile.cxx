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
#include "DmsQueueFile.hxx"
#include "output/OutputState.hxx"
#include "dms/DmsPlaylistState.hxx"
#include "fs/io/TextFile.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "Partition.hxx"
#include "Instance.hxx"
#include "mixer/Volume.hxx"
#include "SongLoader.hxx"
#include "fs/FileSystem.hxx"
#include "util/Domain.hxx"
#include "Log.hxx"

#include <string.h>

static constexpr Domain dms_queue_file_domain("dms_queue_file");

DmsQueueFile::DmsQueueFile(AllocatedPath &&_path, unsigned _interval,
		     Partition &_partition, EventLoop &_loop)
	:TimeoutMonitor(_loop),
	 path(std::move(_path)), path_utf8(path.ToUTF8()),
	 interval(_interval),
	 partition(_partition),
	 df(_partition.df),
	 dc(_partition.dc),
	 prev_playlist_version(0)
{
}

void
DmsQueueFile::RememberVersions()
{
	prev_playlist_version = playlist_state_get_hash(partition.playlist,
							partition.pc);
}

bool
DmsQueueFile::IsModified() const
{
	return prev_playlist_version != playlist_state_get_hash(partition.playlist,
								 partition.pc);
}

inline void
DmsQueueFile::Write(BufferedOutputStream &os)
{
	playlist_state_save(os, partition.playlist, partition.pc);
}

inline bool
DmsQueueFile::Write(OutputStream &os, Error &error)
{
	BufferedOutputStream bos(os);
	Write(bos);
	return bos.Flush(error);
}

void
DmsQueueFile::Write()
{
	const ScopeLock protect(mutex);
	Error error;
	if (path_utf8.empty()) {
		RememberVersions();
		return;
	}
	FormatDefault(dms_queue_file_domain,
		    "Saving dms queue file %s", path_utf8.c_str());

	FileOutputStream fos(path, error);
	if (!fos.IsDefined() || !Write(fos, error) || !fos.Commit(error)) {
		LogError(error);
		return;
	}
	FormatDefault(dms_queue_file_domain,
		    "Saving dms queue file done %s", path_utf8.c_str());

	RememberVersions();
}

void
DmsQueueFile::Read()
{
	const ScopeLock protect(mutex);
	bool success;
	Error error;
	if (path_utf8.empty()) {
		RememberVersions();
		return;
	}
	FormatDefault(dms_queue_file_domain, "Loading dms queue file %s", path_utf8.c_str());

	TextFile file(path, error);
	if (file.HasFailed()) {
		LogError(error);
		return;
	}

#ifdef ENABLE_DATABASE
	const SongLoader song_loader(partition.instance.GetDatabase(error),
				     partition.instance.storage);
#else
	const SongLoader song_loader(nullptr, nullptr, nullptr);
#endif

	const char *line;
	while ((line = file.ReadLine()) != nullptr) {
		success = playlist_state_restore(line, file, song_loader,
					       partition.playlist,
					       partition.pc);
		if (!success)
			FormatError(dms_queue_file_domain,
				    "Unrecognized line in state file: %s",
				    line);
	}

	FormatDefault(dms_queue_file_domain, "Loading dms queue file done %s", path_utf8.c_str());
	CheckModified();
	RememberVersions();
}

void
DmsQueueFile::CheckModified()
{
	if (!IsActive() && IsModified())
		ScheduleSeconds(interval);
}

void
DmsQueueFile::OnTimeout()
{
	Write();
}

bool DmsQueueFile::setPath(std::string str)
{
	const ScopeLock protect(mutex);
	Error error;
	path_utf8 = str;
	path = AllocatedPath::FromUTF8(str.c_str(), error);

	return true;
}

void
DmsQueueFile::Stop()
{
	TimeoutMonitor::Cancel();
}

