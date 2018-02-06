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
#include "DatabasePrint.hxx"
#include "Selection.hxx"
#include "SongFilter.hxx"
#include "SongPrint.hxx"
#include "TimePrint.hxx"
#include "client/Client.hxx"
#include "tag/Tag.hxx"
#include "tag/SetExt.hxx"
#include "LightSong.hxx"
#include "LightDirectory.hxx"
#include "PlaylistInfo.hxx"
#include "Interface.hxx"
#include "fs/Traits.hxx"
#include "dms/DmsConfig.hxx"
#include "Partition.hxx"
#include "storage/StorageInterface.hxx"
#include "util/UriUtil.hxx"

#include <functional>

static const char *
ApplyBaseFlag(const char *uri, bool base)
{
	if (base)
		uri = PathTraitsUTF8::GetBase(uri);
	return uri;
}

static bool
PrintDirectoryInfoBrief(gcc_unused Client &client, gcc_unused bool base, gcc_unused const LightDirectory &directory)
{
	return true;
}

static bool
PrintDirectoryInfoFull(Client &client, bool base, const LightDirectory &directory)
{
	if (!directory.IsRoot()) {
		client_printf(client, "current_directory: %s\n",
				  ApplyBaseFlag(directory.GetPath(), base));
		if (directory.total < std::numeric_limits<unsigned>::max()) {
			client_printf(client, "total: %u\n", directory.total);
		}
	}

	return true;
}

static void
PrintDirectoryURI(Client &client, bool base, const LightDirectory &directory)
{
	client_printf(client, "directory: %s\n",
		      ApplyBaseFlag(directory.GetPath(), base));
}

static bool
PrintDirectoryBrief(Client &client, bool base, const LightDirectory &directory)
{
	if (!directory.IsRoot())
		PrintDirectoryURI(client, base, directory);

	return true;
}

static bool
PrintDirectoryFull(Client &client, bool base, const LightDirectory &directory)
{
	if (!directory.IsRoot()) {
		PrintDirectoryURI(client, base, directory);

		if (directory.total < std::numeric_limits<unsigned>::max()) {
			client_printf(client, "total: %u\n", directory.total);
		}
		if (directory.mtime > 0)
			time_print(client, "Last-Modified", directory.mtime);
	}

	return true;
}

static void
print_playlist_in_directory(Client &client, bool base,
			    const char *directory,
			    const char *name_utf8)
{
	if (base || directory == nullptr)
		client_printf(client, "playlist: %s\n",
			      ApplyBaseFlag(name_utf8, base));
	else
		client_printf(client, "playlist: %s/%s\n",
			      directory, name_utf8);
}

static void
print_playlist_in_directory(Client &client, bool base,
			    const LightDirectory *directory,
			    const char *name_utf8)
{
	if (base || directory == nullptr || directory->IsRoot())
		client_printf(client, "playlist: %s\n", name_utf8);
	else
		client_printf(client, "playlist: %s/%s\n",
			      directory->GetPath(), name_utf8);
}

static bool
PrintSongBrief(Client &client, bool base, const LightSong &song)
{
	song_print_uri(client, song, base);

	if (song.tag->has_playlist)
		/* this song file has an embedded CUE sheet */
		print_playlist_in_directory(client, base,
					    song.directory, song.uri);

	return true;
}

static std::string
get_parent(std::string str)
{
	auto p1 = str.rfind('/');
	if (p1 == std::string::npos) {
		return std::string("Folder");
	}

	auto p2 = str.rfind('/', p1-1);
	if (p2 == std::string::npos) {
		return std::string("Folder");
	}
	return str.substr(p2+1, p1-p2-1);
}

static bool
PrintSongFull(Client &client, bool base, const LightSong &song)
{
	song_print_info(client, song, base);
	if (song.tag && !song.tag->HasType(TAG_ALBUM)) {
		auto str = get_parent(song.GetURI());
		client_printf(client, "%s: %s\n", tag_item_names[TAG_ALBUM], str.c_str());
	}

	if (song.tag->has_playlist)
		/* this song file has an embedded CUE sheet */
		print_playlist_in_directory(client, base,
					    song.directory, song.uri);

	return true;
}

static bool
PrintPlaylistBrief(Client &client, bool base,
		   const PlaylistInfo &playlist,
		   const LightDirectory &directory)
{
	print_playlist_in_directory(client, base,
				    &directory, playlist.name.c_str());
	return true;
}

static bool
PrintPlaylistFull(Client &client, bool base,
		  const PlaylistInfo &playlist,
		  const LightDirectory &directory)
{
	print_playlist_in_directory(client, base,
				    &directory, playlist.name.c_str());

	if (playlist.mtime > 0)
		time_print(client, "Last-Modified", playlist.mtime);

	return true;
}

bool
db_selection_print(Client &client, DatabaseSelection &selection,
		   bool full, bool base,
		   unsigned window_start, unsigned window_end,
		   Error &error)
{
	const Database *db = client.GetDatabase(error);
	bool result  = false;

	DmsConfig	&df = client.partition.df;
	DmsSource &source = df.source;
	if (source.value == SOURCE_UPNP
		&& selection.uri.empty()) {
		selection.uri = source.getName();
	}

	if  (db == nullptr)
		return false;

	unsigned i = 0;

	using namespace std::placeholders;
	const auto di = selection.filter == nullptr
		? std::bind(full ? PrintDirectoryInfoFull : PrintDirectoryInfoBrief,
			    std::ref(client), base, _1)
		: VisitDirectoryInfo();
	const auto d = selection.filter == nullptr
		? std::bind(full ? PrintDirectoryFull : PrintDirectoryBrief,
			    std::ref(client), base, _1)
		: VisitDirectory();
	VisitSong s = std::bind(full ? PrintSongFull : PrintSongBrief,
				std::ref(client), base, _1);
	const auto p = selection.filter == nullptr
		? std::bind(full ? PrintPlaylistFull : PrintPlaylistBrief,
			    std::ref(client), base, _1, _2)
		: VisitPlaylist();

	if (window_start > 0 ||
	    window_end < (unsigned)std::numeric_limits<int>::max()) {
		s = [s, window_start, window_end, &i](const LightSong &song,
						      Error &error2){
			const bool in_window = i >= window_start && i < window_end;
			++i;
			return !in_window || s(song, error2);
		};
	}

	result |= db->Visit(selection, di, d, s, p, error);

	return  result;
}

bool
db_selection_print(Client &client, DatabaseSelection &selection,
		   bool full, bool base,
		   Error &error)
{
	return db_selection_print(client, selection, full, base,
				  0, std::numeric_limits<int>::max(),
				  error);
}

static bool
PrintSongURIVisitor(Client &client, const LightSong &song)
{
	song_print_uri(client, song);

	return true;
}

static bool
PrintUniqueTag(Client &client, TagType tag_type,
	       const Tag &tag)
{
	const char *value = tag.GetValue(tag_type);
	assert(value != nullptr);
	client_printf(client, "%s: %s\n", tag_item_names[tag_type], value);

	for (const auto &item : tag)
		if (item.type != tag_type)
			client_printf(client, "%s: %s\n",
				      tag_item_names[item.type], item.value);

	return true;
}

bool
PrintUniqueTags(Client &client, unsigned type, uint32_t group_mask,
		const SongFilter *filter,
		Error &error)
{
	DmsConfig	&df = client.partition.df;
	DmsSource &source = df.source;
	const Database *db = client.GetDatabase(error);
	if (db == nullptr)
		return false;

	std::string uri = source.isUpnp() ? source.getName().c_str() : "";
	bool ignore_repeat = source.isUpnp() ? true : false;

	const DatabaseSelection selection(uri.c_str(), true, ignore_repeat, filter);

	if (type == LOCATE_TAG_FILE_TYPE) {
		using namespace std::placeholders;
		const auto f = std::bind(PrintSongURIVisitor,
					 std::ref(client), _1);
		return db->Visit(selection, f, error);
	} else {
		assert(type < TAG_NUM_OF_ITEM_TYPES);

		using namespace std::placeholders;
		const auto f = std::bind(PrintUniqueTag, std::ref(client),
					 (TagType)type, _1);
		return db->VisitUniqueTags(selection, (TagType)type,
					   group_mask,
					   f, error);
	}
}
