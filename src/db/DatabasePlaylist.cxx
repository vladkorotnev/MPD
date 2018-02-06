/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
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
#include "DatabasePlaylist.hxx"
#include "DatabaseSong.hxx"
#include "Selection.hxx"
#include "PlaylistFile.hxx"
#include "Interface.hxx"
#include "DetachedSong.hxx"
#include "storage/StorageInterface.hxx"
#include "db/Selection.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "util/Error.hxx"
#include "playlist/SongEnumerator.hxx"
#include "playlist/PlaylistSong.hxx"
#include "playlist/PlaylistAny.hxx"
#include "PlaylistSave.hxx"
#include "PlaylistFile.hxx"
#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"
#include "util/StringUtil.hxx"
#include "PlaylistError.hxx"
#include "fs/FileSystem.hxx"

#include <functional>

#ifdef ENABLE_DATABASE
#include "SongLoader.hxx"
#endif

static bool
AddSong(const Storage &storage, const char *playlist_path_utf8,
	const LightSong &song, Error &error)
{
	return spl_append_song(playlist_path_utf8,
			       DatabaseDetachSong(storage, song),
			       error);
}

bool
search_add_to_playlist(const Database &db, const Storage &storage,
		       const char *playlist_path_utf8,
		       const DatabaseSelection &selection,
		       Error &error)
{
	using namespace std::placeholders;
	const auto f = std::bind(AddSong, std::ref(storage),
				 playlist_path_utf8, _1, _2);
	return db.Visit(selection, f, error);
}

static bool
playlist_load_into_playlist(const char *uri, SongEnumerator &e,
			 unsigned start_index, unsigned end_index,
			 const char *dest,
			 const SongLoader &loader,
			 Error &error, BufferedOutputStream *bos)
{
	const std::string base_uri = uri != nullptr
		? PathTraitsUTF8::GetParent(uri)
		: std::string(".");

	DetachedSong *song;
	for (unsigned i = 0;
	     i < end_index && (song = e.NextSong()) != nullptr;
	     ++i) {
		if (i < start_index) {
			/* skip songs before the start index */
			delete song;
			continue;
		}

		if (!playlist_check_translate_song(*song, base_uri.c_str(),
						   loader)) {
			delete song;
			continue;
		}

		if (bos != nullptr) {
			playlist_print_song(*bos, *song);
		}
		bool ret = spl_append_song(dest,std::move(*song), error);
		delete song;
		if (!ret)
			return ret;
	}

	return true;
}

bool
playlist_open_into_playlist(const char *uri,
			 unsigned start_index, unsigned end_index,
			 const char *dest,
			 const SongLoader &loader,
			 Error &error)
{
	Mutex mutex;
	Cond cond;
	FileOutputStream *fos = nullptr;
	BufferedOutputStream *bos = nullptr;
	std::string new_uri = uri;

	auto playlist = playlist_open_any(uri,
#ifdef ENABLE_DATABASE
					  loader.GetStorage(),
#endif
					  mutex, cond);
	if (playlist == nullptr) {
		error.Set(playlist_domain, int(PlaylistResult::NO_SUCH_LIST),
			  "No such playlist");
		return false;
	}

	bool result =
		playlist_load_into_playlist(uri, *playlist,
					 start_index, end_index,
					 dest, loader, error, bos);
	delete playlist;
	if (bos!=nullptr && bos->Flush(error) && fos!= nullptr && fos->Commit(error)) {
		const auto path_fs = spl_map_to_fs(uri, error);
		const auto new_path_fs = spl_map_to_fs(new_uri.c_str(), error);
		RemoveFile(path_fs);
		RenameFile(new_path_fs, path_fs);
	}
	return result;
}

