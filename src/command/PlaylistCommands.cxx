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
#include "PlaylistCommands.hxx"
#include "db/DatabasePlaylist.hxx"
#include "db/Selection.hxx"
#include "CommandError.hxx"
#include "PlaylistPrint.hxx"
#include "PlaylistSave.hxx"
#include "PlaylistFile.hxx"
#include "db/PlaylistVector.hxx"
#include "SongLoader.hxx"
#include "BulkEdit.hxx"
#include "playlist/PlaylistQueue.hxx"
#include "playlist/Print.hxx"
#include "queue/Playlist.hxx"
#include "TimePrint.hxx"
#include "client/Client.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "ls.hxx"
#include "Mapper.hxx"
#include "fs/AllocatedPath.hxx"
#include "util/UriUtil.hxx"
#include "util/Error.hxx"
#include "util/ConstBuffer.hxx"
#include "PlaylistError.hxx"
#include "PlaylistFile.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/FileSystem.hxx"

#define DMS_SOURCE_INVALID		"Current source invalid or incomplete"

bool
playlist_commands_available()
{
	return !map_spl_path().IsNull();
}

static void
print_spl_list(Client &client, const PlaylistVector &list)
{
	DmsConfig	&df = client.partition.df;
	std::string prefix = df.source.getQueuePath();
	
	for (const auto &i : list) {
		if (!prefix.empty() &&
			checkPlaylistPrefix(i.name.c_str(), prefix.c_str())) {
			client_printf(client, "playlist: %s\n", stripPrefix(i.name.c_str(), prefix.c_str()));

			if (i.mtime > 0)
				time_print(client, "Last-Modified", i.mtime);
		}
	}
}

CommandResult
handle_save(Client &client, ConstBuffer<const char *> args)
{
	Error error;
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args.front());
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) invalid",
				  args.front());
		return CommandResult::ERROR;
	}
	return spl_save_playlist(playlist.c_str(), client.playlist, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_load(Client &client, ConstBuffer<const char *> args)
{
	unsigned start_index, end_index;
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args.front());
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) invalid",
				  args.front());
		return CommandResult::ERROR;
	}

	if (args.size < 2) {
		start_index = 0;
		end_index = unsigned(-1);
	} else if (!check_range(client, &start_index, &end_index, args[1]))
		return CommandResult::ERROR;

	const ScopeBulkEdit bulk_edit(client.partition);

	Error error;
	const SongLoader loader(client);
	if (!playlist_open_into_queue(playlist.c_str(),
				      start_index, end_index,
				      client.playlist,
				      client.player_control, loader, error))
		return print_error(client, error);

	return CommandResult::OK;
}

CommandResult
handle_listplaylist(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args.front());
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	const char *name = playlist.c_str();

	if (playlist_file_print(client, name, false))
		return CommandResult::OK;

	Error error;
	return spl_print(client, name, false, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_listplaylistinfo(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args.front());
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	const char *name = playlist.c_str();

	if (playlist_file_print(client, name, true))
		return CommandResult::OK;

	Error error;
	return spl_print(client, name, true, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_rm(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args.front());
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	const char *name = playlist.c_str();

	Error error;
	return spl_delete(name, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_rename(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string old_name = df.source.getPlaylistName(args[0]);
	std::string new_name = df.source.getPlaylistName(args[1]);
	if (old_name.empty() || new_name.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) or (%s) not exist",
				  args.front(), args[1]);
		return CommandResult::ERROR;
	}

	Error error;
	return spl_rename(old_name.c_str(), new_name.c_str(), error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_playlistdelete(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string name = df.source.getPlaylistName(args[0]);
	if (name.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	unsigned from;

	if (!check_unsigned(client, &from, args[1]))
		return CommandResult::ERROR;

	Error error;
	return spl_remove_index(name.c_str(), from, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_playlistmove(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string name = df.source.getPlaylistName(args[0]);
	if (name.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	unsigned from, to;

	if (!check_unsigned(client, &from, args[1]))
		return CommandResult::ERROR;
	if (!check_unsigned(client, &to, args[2]))
		return CommandResult::ERROR;

	Error error;
	return spl_move_index(name.c_str(), from, to, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_playlistclear(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string name = df.source.getPlaylistName(args[0]);
	if (name.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}

	Error error;
	return spl_clear(name.c_str(), error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_playlistload(Client &client, ConstBuffer<const char *> args)
{
	unsigned start_index, end_index;
	DmsConfig	&df = client.partition.df;
	std::string dst = df.source.getPlaylistName(args.front());
	std::string src = df.source.getPlaylistName(args[1]);
	if (dst.empty() || src.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s)(%s) invalid",
				  args.front(), args[1]);
		return CommandResult::ERROR;
	}

	if (args.size < 3) {
		start_index = 0;
		end_index = unsigned(-1);
	} else if (!check_range(client, &start_index, &end_index, args[2]))
		return CommandResult::ERROR;

	const ScopeBulkEdit bulk_edit(client.partition);

	Error error;
	const SongLoader loader(client);
	if (!playlist_open_into_playlist(src.c_str(),
				      start_index, end_index,
				      dst.c_str(),
				      loader, error))
		return print_error(client, error);

	return CommandResult::OK;
}

CommandResult
handle_playlistadd(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args[0]);
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	const char *uri = args[1];

	unsigned start = 0, end = std::numeric_limits<unsigned>::max();
	if (args.size == 3 && !check_range(client, &start, &end, args.back()))
		return CommandResult::ERROR;
	DatabaseSelection selection(uri, true);
	selection.window_start = start;
	selection.window_end = end;

	bool success;
	Error error;
	if (uri_has_scheme(uri)) {
		const SongLoader loader(client);
		success = spl_append_uri(playlist.c_str(), loader, uri, error);
	} else {
#ifdef ENABLE_DATABASE
		const Database *db = client.GetDatabase(error);
		if  (db == nullptr)
			return print_error(client, error);

		success = search_add_to_playlist(*db, *client.GetStorage(),
						 playlist.c_str(), selection,
						 error);
#else
		success = false;
#endif
	}

	if (!success && !error.IsDefined()) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");
		return CommandResult::ERROR;
	}

	return success ? CommandResult::OK : print_error(client, error);
}

CommandResult
handle_playlistsave(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args[0]);
	Error error;
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}

	const auto path_fs = spl_map_to_fs(playlist.c_str(), error);
	if (path_fs.IsNull())
		return print_error(client, error);

	if (FileExists(path_fs)) {
		error.Set(playlist_domain, int(PlaylistResult::LIST_EXISTS),
			  "Playlist already exists");
		return print_error(client, error);
	}

	const char *const uri = args[1];

	unsigned start = 0, end = std::numeric_limits<unsigned>::max();
	if (args.size == 3 && !check_range(client, &start, &end, args.back()))
		return CommandResult::ERROR;
	DatabaseSelection selection(uri, true);
	selection.window_start = start;
	selection.window_end = end;

	bool success;
	if (uri_has_scheme(uri)) {
		const SongLoader loader(client);
		success = spl_append_uri(playlist.c_str(), loader, uri, error);
	} else {
#ifdef ENABLE_DATABASE
		const Database *db = client.GetDatabase(error);
		if  (db == nullptr)
			return print_error(client, error);

		success = search_add_to_playlist(*db, *client.GetStorage(),
						 playlist.c_str(), selection,
						 error);
#else
		success = false;
#endif
	}

	if (!success && !error.IsDefined()) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");
		return CommandResult::ERROR;
	}

	return success ? CommandResult::OK : print_error(client, error);
}

CommandResult
handle_listplaylists(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	Error error;
	const auto list = ListPlaylistFiles(error);
	if (list.empty() && error.IsDefined())
		return print_error(client, error);

	print_spl_list(client, list);
	return CommandResult::OK;
}

CommandResult
handle_loadQueue(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getQueuePath();
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	client.partition.ClearQueue();

	const ScopeBulkEdit bulk_edit(client.partition);

	Error error;
	const SongLoader loader(client);
	if (!playlist_open_into_queue(playlist.c_str(),
				      0, unsigned(-1),
				      client.playlist,
				      client.player_control, loader, error))
		return print_error(client, error);

	return CommandResult::OK;
}

CommandResult
handle_saveQueue(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	Error error;
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getQueuePath();
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "%s",
				  DMS_SOURCE_INVALID);
		return CommandResult::ERROR;
	}
	spl_delete(playlist.c_str(), error);
	return spl_save_playlist(playlist.c_str(), client.playlist, error)
		? CommandResult::OK
		: print_error(client, error);
}

CommandResult
handle_addQueueToPlaylist(Client &client, ConstBuffer<const char *> args)
{
	Error error;
	DmsConfig	&df = client.partition.df;
	std::string playlist = df.source.getPlaylistName(args.front());
	if (playlist.empty()) {
		command_error(client, ACK_ERROR_NO_EXIST, "the playlist(%s) not exist",
				  args.front());
		return CommandResult::ERROR;
	}
	unsigned start = 0, end = std::numeric_limits<unsigned>::max();

	if (args.size >= 2 && !check_range(client, &start, &end, args.back()))
		return CommandResult::ERROR;
	return spl_append_queue(playlist.c_str(), client.playlist.queue, start, end, error)
		? CommandResult::OK
		: print_error(client, error);
}

