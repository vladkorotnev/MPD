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
#include "DmsPlaylistState.hxx"
#include "PlaylistError.hxx"
#include "queue/Playlist.hxx"
#include "queue/QueueSave.hxx"
#include "fs/io/TextFile.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "PlayerControl.hxx"
#include "config/ConfigGlobal.hxx"
#include "config/ConfigOption.hxx"
#include "util/CharUtil.hxx"
#include "util/StringUtil.hxx"
#include "Log.hxx"

#include <string.h>
#include <stdlib.h>

#define PLAYLIST_STATE_FILE_STATE		"state: "
#define PLAYLIST_STATE_FILE_RANDOM		"random: "
#define PLAYLIST_STATE_FILE_REPEAT		"repeat: "
#define PLAYLIST_STATE_FILE_SINGLE		"single: "
#define PLAYLIST_STATE_FILE_CONSUME		"consume: "
#define PLAYLIST_STATE_FILE_CURRENT		"current: "
#define PLAYLIST_STATE_FILE_TIME		"time: "
#define PLAYLIST_STATE_FILE_CROSSFADE		"crossfade: "
#define PLAYLIST_STATE_FILE_MIXRAMPDB		"mixrampdb: "
#define PLAYLIST_STATE_FILE_MIXRAMPDELAY	"mixrampdelay: "
#define PLAYLIST_STATE_FILE_PLAYLIST_BEGIN	"playlist_begin"
#define PLAYLIST_STATE_FILE_PLAYLIST_END	"playlist_end"

#define PLAYLIST_STATE_FILE_STATE_PLAY		"play"
#define PLAYLIST_STATE_FILE_STATE_PAUSE		"pause"
#define PLAYLIST_STATE_FILE_STATE_STOP		"stop"

void
playlist_state_save(BufferedOutputStream &os, const struct playlist &playlist,
		    PlayerControl &pc)
{
	const auto player_status = pc.GetStatus();

	os.Write(PLAYLIST_STATE_FILE_STATE);

	if (playlist.playing) {
		switch (player_status.state) {
		case PlayerState::PAUSE:
			os.Write(PLAYLIST_STATE_FILE_STATE_PAUSE "\n");
			break;
		default:
			os.Write(PLAYLIST_STATE_FILE_STATE_PLAY "\n");
		}
		os.Format(PLAYLIST_STATE_FILE_CURRENT "%i\n",
			  playlist.queue.OrderToPosition(playlist.current));
		os.Format(PLAYLIST_STATE_FILE_TIME "%f\n",
			  player_status.elapsed_time.ToDoubleS());
	} else {
		os.Write(PLAYLIST_STATE_FILE_STATE_STOP "\n");

		if (playlist.current >= 0)
			os.Format(PLAYLIST_STATE_FILE_CURRENT "%i\n",
				playlist.queue.OrderToPosition(playlist.current));
	}

	os.Write(PLAYLIST_STATE_FILE_PLAYLIST_BEGIN "\n");
	queue_save(os, playlist.queue);
	os.Write(PLAYLIST_STATE_FILE_PLAYLIST_END "\n");
}

static void
playlist_state_load(TextFile &file, const SongLoader &song_loader,
		    struct playlist &playlist)
{
	const char *line = file.ReadLine();
	if (line == nullptr) {
		LogWarning(playlist_domain, "No playlist in state file");
		return;
	}

	while (!StringStartsWith(line, PLAYLIST_STATE_FILE_PLAYLIST_END)) {
		queue_load_song(file, song_loader, line, playlist.queue);

		line = file.ReadLine();
		if (line == nullptr) {
			LogWarning(playlist_domain,
				   "'" PLAYLIST_STATE_FILE_PLAYLIST_END
				   "' not found in state file");
			break;
		}
	}

	playlist.queue.IncrementVersion();
}

bool
playlist_state_restore(const char *line, TextFile &file,
		       const SongLoader &song_loader,
		       struct playlist &playlist, PlayerControl &pc)
{
	int current = -1;
	SongTime seek_time = SongTime::zero();

	if (!StringStartsWith(line, PLAYLIST_STATE_FILE_STATE))
		return false;

	line += sizeof(PLAYLIST_STATE_FILE_STATE) - 1;

	PlayerState state;
	if (strcmp(line, PLAYLIST_STATE_FILE_STATE_PLAY) == 0)
		state = PlayerState::PLAY;
	else if (strcmp(line, PLAYLIST_STATE_FILE_STATE_PAUSE) == 0)
		state = PlayerState::PAUSE;
	else
		state = PlayerState::STOP;

	while ((line = file.ReadLine()) != nullptr) {
		if (StringStartsWith(line, PLAYLIST_STATE_FILE_TIME)) {
			double seconds = atof(line + strlen(PLAYLIST_STATE_FILE_TIME));
			seek_time = SongTime::FromS(seconds);
		} else if (StringStartsWith(line, PLAYLIST_STATE_FILE_CURRENT)) {
			current = atoi(&(line
					 [strlen
					  (PLAYLIST_STATE_FILE_CURRENT)]));
		} else if (StringStartsWith(line,
					    PLAYLIST_STATE_FILE_PLAYLIST_BEGIN)) {
			playlist_state_load(file, song_loader, playlist);
		}
	}

	if (!playlist.queue.IsEmpty()) {
		if (!playlist.queue.IsValidPosition(current))
			current = 0;

		if (state == PlayerState::PLAY &&
		    config_get_bool(ConfigOption::RESTORE_PAUSED, false))
			/* the user doesn't want MPD to auto-start
			   playback after startup; fall back to
			   "pause" */
			state = PlayerState::PAUSE;

		/* enable all devices for the first time; this must be
		   called here, after the audio output states were
		   restored, before playback begins */
		if (state != PlayerState::STOP)
			pc.UpdateAudio();

		if (state == PlayerState::STOP /* && config_option */)
			playlist.current = current;
		else if (seek_time.count() == 0)
			playlist.PlayPosition(pc, current);
		else
			playlist.SeekSongPosition(pc, current, seek_time);

		if (state == PlayerState::PAUSE)
			pc.Pause();
	}

	return true;
}

unsigned
playlist_state_get_hash(const playlist &playlist,
			PlayerControl &pc)
{
	const auto player_status = pc.GetStatus();

	return playlist.queue.version ^
		(player_status.state != PlayerState::STOP
		 ? (player_status.elapsed_time.ToS() << 8)
		 : 0) ^
		(playlist.current >= 0
		 ? (playlist.queue.OrderToPosition(playlist.current) << 16)
		 : 0) ^
		((int)pc.GetCrossFade() << 20) ^
		(unsigned(player_status.state) << 24) ^
		(playlist.queue.random << 27) ^
		(playlist.queue.repeat << 28) ^
		(playlist.queue.single << 29) ^
		(playlist.queue.consume << 30) ^
		(playlist.queue.random << 31);
}
