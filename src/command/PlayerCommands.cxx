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
#include "PlayerCommands.hxx"
#include "CommandError.hxx"
#include "queue/Playlist.hxx"
#include "PlaylistPrint.hxx"
#include "client/Client.hxx"
#include "mixer/Volume.hxx"
#include "Partition.hxx"
#include "Instance.hxx"
#include "protocol/Result.hxx"
#include "protocol/ArgParser.hxx"
#include "AudioFormat.hxx"
#include "ReplayGainConfig.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Domain.hxx"
#include "dms/DmsConfig.hxx"
#include "dms/Context.hxx"
#ifdef ENABLE_DATABASE
#include "db/update/Service.hxx"
#include "db/Interface.hxx"
#endif
#include "neighbor/Glue.hxx"
#include "neighbor/Info.hxx"
#include "Log.hxx"
#include "TagPrint.hxx"

#define COMMAND_STATUS_STATE            "state"
#define COMMAND_STATUS_REPEAT           "repeat"
#define COMMAND_STATUS_SINGLE           "single"
#define COMMAND_STATUS_CONSUME          "consume"
#define COMMAND_STATUS_RANDOM           "random"
#define COMMAND_STATUS_PLAYLIST         "playlist"
#define COMMAND_STATUS_PLAYLIST_LENGTH  "playlistlength"
#define COMMAND_STATUS_SONG             "song"
#define COMMAND_STATUS_SONGID           "songid"
#define COMMAND_STATUS_NEXTSONG         "nextsong"
#define COMMAND_STATUS_NEXTSONGID       "nextsongid"
#define COMMAND_STATUS_TIME             "time"
#define COMMAND_STATUS_BITRATE          "bitrate"
#define COMMAND_STATUS_ERROR            "error"
#define COMMAND_STATUS_CROSSFADE	"xfade"
#define COMMAND_STATUS_MIXRAMPDB	"mixrampdb"
#define COMMAND_STATUS_MIXRAMPDELAY	"mixrampdelay"
#define COMMAND_STATUS_AUDIO		"audio"
#define COMMAND_STATUS_UPDATING_DB	"updating_db"
#define COMMAND_STATUS_SOURCE		"source"
#define COMMAND_STATUS_SOURCE_NAME	"source_name"
#define COMMAND_STATUS_ICON_URL		"icon_url"
#define COMMAND_STATUS_VOLUME_POLICY "volume_policy"
#define COMMAND_STATUS_SRC			"SRC"
#define COMMAND_STATUS_TUBE			"tube"
#define COMMAND_STATUS_RATE			"rate"
#define COMMAND_STATUS_BLUETOOTH_STATE	"bluetooth_state"

static constexpr Domain player_command_domain("player_command");

CommandResult
handle_play(Client &client, ConstBuffer<const char *> args)
{
	int song = -1;

	FormatDefault(player_command_domain, "%s %d", __func__, __LINE__);
	DmsConfig	&df = client.partition.df;
	if (!df.source.isMaster()) {
		client_printf(client, "not master source, ignored! \n");
		return CommandResult::OK;
	}
	if (!args.IsEmpty() && !check_int(client, &song, args.front()))
		return CommandResult::ERROR;
	PlaylistResult result = client.partition.PlayPosition(song);
	return print_playlist_result(client, result);
}

CommandResult
handle_playid(Client &client, ConstBuffer<const char *> args)
{
	int id = -1;


	DmsConfig	&df = client.partition.df;
	if (!df.source.isMaster()) {
		client_printf(client, "not master source, ignored! \n");
		return CommandResult::OK;
	}
	if (!args.IsEmpty() && !check_int(client, &id, args.front()))
		return CommandResult::ERROR;

	PlaylistResult result = client.partition.PlayId(id);
	auto ret = print_playlist_result(client, result);
	return ret;
}

CommandResult
handle_stop(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	client.partition.Stop();
	return CommandResult::OK;
}

CommandResult
handle_currentsong(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	if (df.source.isBluetooth()) {
		client_printf(client, "file: bluetooth\n");
		const Tag &tag = df.bluetooth.tag;
		if (!tag.duration.IsNegative())
			client_printf(client, "Time: %i\n"
					  "duration: %1.3f\n",
					  tag.duration.RoundS(),
					  tag.duration.ToDoubleS());
		for (const auto &i : tag) {
			client_printf(client, "%s: %s\n", tag_item_names[i.type], i.value);
		}
		client_printf(client, "Pos: 0\nId: %u\n", df.bluetooth.id);
		return CommandResult::OK;
	}

	playlist_print_current(client, client.playlist);
	return CommandResult::OK;
}

CommandResult
handle_pause(Client &client, ConstBuffer<const char *> args)
{
	if (!args.IsEmpty()) {
		bool pause_flag;
		if (!check_bool(client, &pause_flag, args.front()))
			return CommandResult::ERROR;

		client.player_control.SetPause(pause_flag);
	} else
		client.player_control.Pause();

	return CommandResult::OK;
}

CommandResult
handle_status(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	const char *state = nullptr;
	int song;
	DmsConfig	&df = client.partition.df;

	const auto player_status = df.source.isBluetooth()
			? df.bluetooth.getStatus()
			: client.player_control.GetStatus();

	switch (player_status.state) {
	case PlayerState::STOP:
		state = "stop";
		break;
	case PlayerState::PAUSE:
		state = "pause";
		break;
	case PlayerState::PLAY:
		state = "play";
		break;
	}

	const playlist &playlist = client.playlist;
	client_printf(client,
			  "poweron: %s\n"
	    	  "playmode: %s\n"
			  "mute: %i\n"
	    	  "volume_db: %.1f\n"
  			  "volume: %i\n"
#if 0//ndef ENABLE_DMS
		      COMMAND_STATUS_REPEAT ": %i\n"
		      COMMAND_STATUS_RANDOM ": %i\n"
		      COMMAND_STATUS_SINGLE ": %i\n"
#endif
		      COMMAND_STATUS_CONSUME ": %i\n"
		      COMMAND_STATUS_PLAYLIST ": %li\n"
		      COMMAND_STATUS_PLAYLIST_LENGTH ": %i\n"
		      COMMAND_STATUS_MIXRAMPDB ": %f\n"
		      COMMAND_STATUS_STATE ": %s\n",
			  client.context.poweron.c_str(),
			  client.context.playmode.c_str(),
			  client.context.mute,
			  df.getVolume().toDb(),
			  df.getVolume().toPercentage(),
#if 0//ndef ENABLE_DMS
		      playlist.GetRepeat(),
		      playlist.GetRandom(),
		      playlist.GetSingle(),
#endif
		      playlist.GetConsume(),
		      (unsigned long)playlist.GetVersion(),
		      df.source.isBluetooth()
		      ? 1
		      : playlist.GetLength(),
		      client.player_control.GetMixRampDb(),
		      state);

	if (client.player_control.GetCrossFade() > 0)
		client_printf(client,
			      COMMAND_STATUS_CROSSFADE ": %i\n",
			      int(client.player_control.GetCrossFade() + 0.5));

	if (client.player_control.GetMixRampDelay() > 0)
		client_printf(client,
			      COMMAND_STATUS_MIXRAMPDELAY ": %f\n",
			      client.player_control.GetMixRampDelay());

	if (df.source.isBluetooth()) {
		client_printf(client,
				  COMMAND_STATUS_SONG ": %i\n"
				  COMMAND_STATUS_SONGID ": %u\n",
				  0, df.bluetooth.id);
	} else {
		song = playlist.GetCurrentPosition();
		if (song >= 0) {
			client_printf(client,
				      COMMAND_STATUS_SONG ": %i\n"
				      COMMAND_STATUS_SONGID ": %u\n",
				      song, playlist.PositionToId(song));
		}
	}

	std::string rate = current_rate_tostring(client);
	std::string src = df.src.toString();
	if (player_status.state != PlayerState::STOP) {
		client_printf(client,
			      COMMAND_STATUS_TIME ": %i:%i\n"
			      "elapsed: %1.3f\n"
			      COMMAND_STATUS_BITRATE ": %u\n",
			      player_status.elapsed_time.RoundS(),
			      player_status.total_time.IsNegative()
			      ? 0u
			      : unsigned(player_status.total_time.RoundS()),
			      player_status.elapsed_time.ToDoubleS(),
			      player_status.bit_rate);

		if (!player_status.total_time.IsNegative())
			client_printf(client, "duration: %1.3f\n",
				      player_status.total_time.ToDoubleS());

		if (player_status.audio_format.IsDefined()) {
			struct audio_format_string af_string;
			AudioFormat fmt = player_status.audio_format;
			char s = src.front();
			if (s == 'D' || s == 'd') {
				fmt.format2 = SampleFormat::DSD;
			} else if ((src != rate) && isdigit(s)) {
				if (fmt.format2 == SampleFormat::S32
					|| fmt.format2 == SampleFormat::DOUBLE) {
					fmt.format2 = fmt.format2;
				} else if (fmt.format == SampleFormat::S32
					|| fmt.format == SampleFormat::DOUBLE) {
					fmt.format2 = fmt.format;
				} else {
					fmt.format2 = SampleFormat::S32;
				}
			}

			client_printf(client,
				      COMMAND_STATUS_AUDIO ": %s\n",
				      audio_format_to_string(player_status.audio_format,
							     &af_string));
			client_printf(client,
				      "o_audio: %s\n",
				      audio_format_to_string(fmt,
							     &af_string));
		}
	}

	client_printf(client, COMMAND_STATUS_SOURCE ": %s\n",
			df.source.uri.c_str());
	client_printf(client, COMMAND_STATUS_SOURCE_NAME ": %s\n",
			df.source.source_name.c_str());
	if (df.source.isUpnp()) {
		client_printf(client, COMMAND_STATUS_ICON_URL ": %s\n",
				df.source.icon.c_str());
	}
	client_printf(client, COMMAND_STATUS_VOLUME_POLICY ": %s\n",
			df.volume_policy.toString().c_str());
	client_printf(client, COMMAND_STATUS_SRC ": %s\n",
			src.c_str());
	client_printf(client, COMMAND_STATUS_RATE ": %s\n",
			rate.c_str());
	if (df.source.isBluetooth()) {
		client_printf(client, COMMAND_STATUS_BLUETOOTH_STATE ": %s\n",
			df.bluetooth.stateString().c_str());
	}

#ifdef ENABLE_DATABASE
	const UpdateService *update_service = client.partition.instance.update;
	unsigned updateJobId = update_service != nullptr
		? update_service->GetId()
		: 0;
	if (updateJobId != 0) {
		client_printf(client,
			      COMMAND_STATUS_UPDATING_DB ": %i\n",
			      updateJobId);
	}
#endif

	Error error = client.player_control.LockGetError();
	if (error.IsDefined())
		client_printf(client,
			      COMMAND_STATUS_ERROR ": %s\n",
			      error.GetMessage());

	song = playlist.GetNextPosition();
	if (song >= 0) {
		client_printf(client,
			      COMMAND_STATUS_NEXTSONG ": %i\n"
			      COMMAND_STATUS_NEXTSONGID ": %u\n",
			      song, playlist.PositionToId(song));
	}
	
#ifdef ENABLE_NEIGHBOR_PLUGINS
const NeighborGlue *const neighbors =
	client.partition.instance.neighbors;
	if (neighbors != nullptr &&
		neighbors->Scanning()) {
		client_printf(client,
			      "scanning_neighbors: %d\n", neighbors->Scanning());
	}
#endif
	client_printf(client,
		"brightness: %u\n"
		"brightness_level: %s\n",
		df.brightness.toPercentage(),
		df.brightness.toString().c_str());

	if (player_status.state != PlayerState::STOP) {
		client_printf(client,
			      "buffered: %1.3f\n",
			      player_status.bufferd_time.ToDoubleS());
	}

	return CommandResult::OK;
}

CommandResult
handle_next(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	playlist &playlist = client.playlist;

	/* single mode is not considered when this is user who
	 * wants to change song. */
	const bool single = playlist.queue.single;
	playlist.queue.single = false;

	client.partition.PlayNext();

	playlist.queue.single = single;
	return CommandResult::OK;
}

CommandResult
handle_previous(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	client.partition.PlayPrevious();
	return CommandResult::OK;
}

CommandResult
handle_repeat(Client &client, ConstBuffer<const char *> args)
{
	bool status;
	if (!check_bool(client, &status, args.front()))
		return CommandResult::ERROR;

	client.partition.SetRepeat(status);
	return CommandResult::OK;
}

CommandResult
handle_single(Client &client, ConstBuffer<const char *> args)
{
	bool status;
	if (!check_bool(client, &status, args.front()))
		return CommandResult::ERROR;

	client.partition.SetSingle(status);
	return CommandResult::OK;
}

CommandResult
handle_consume(Client &client, ConstBuffer<const char *> args)
{
	bool status;
	if (!check_bool(client, &status, args.front()))
		return CommandResult::ERROR;

	client.partition.SetConsume(status);
	return CommandResult::OK;
}

CommandResult
handle_random(Client &client, ConstBuffer<const char *> args)
{
	bool status;
	if (!check_bool(client, &status, args.front()))
		return CommandResult::ERROR;

	client.partition.SetRandom(status);
	client.partition.outputs.SetReplayGainMode(replay_gain_get_real_mode(client.partition.GetRandom()));
	return CommandResult::OK;
}

CommandResult
handle_clearerror(gcc_unused Client &client, gcc_unused ConstBuffer<const char *> args)
{
	client.player_control.ClearError();
	return CommandResult::OK;
}

CommandResult
handle_seek(Client &client, ConstBuffer<const char *> args)
{
	unsigned song;
	SongTime seek_time;

	if (!check_unsigned(client, &song, args[0]))
		return CommandResult::ERROR;
	if (!ParseCommandArg(client, seek_time, args[1]))
		return CommandResult::ERROR;

	PlaylistResult result =
		client.partition.SeekSongPosition(song, seek_time);
	return print_playlist_result(client, result);
}

CommandResult
handle_seekid(Client &client, ConstBuffer<const char *> args)
{
	unsigned id;
	SongTime seek_time;

	if (!check_unsigned(client, &id, args[0]))
		return CommandResult::ERROR;
	if (!ParseCommandArg(client, seek_time, args[1]))
		return CommandResult::ERROR;

	PlaylistResult result =
		client.partition.SeekSongId(id, seek_time);
	return print_playlist_result(client, result);
}

CommandResult
handle_seekcur(Client &client, ConstBuffer<const char *> args)
{
	const char *p = args.front();
	bool relative = *p == '+' || *p == '-';
	SignedSongTime seek_time;
	if (!ParseCommandArg(client, seek_time, p))
		return CommandResult::ERROR;

	PlaylistResult result =
		client.partition.SeekCurrent(seek_time, relative);
	return print_playlist_result(client, result);
}

CommandResult
handle_crossfade(Client &client, ConstBuffer<const char *> args)
{
	unsigned xfade_time;

	if (!check_unsigned(client, &xfade_time, args.front()))
		return CommandResult::ERROR;
	client.player_control.SetCrossFade(xfade_time);

	return CommandResult::OK;
}

CommandResult
handle_mixrampdb(Client &client, ConstBuffer<const char *> args)
{
	float db;

	if (!check_float(client, &db, args.front()))
		return CommandResult::ERROR;
	client.player_control.SetMixRampDb(db);

	return CommandResult::OK;
}

CommandResult
handle_mixrampdelay(Client &client, ConstBuffer<const char *> args)
{
	float delay_secs;

	if (!check_float(client, &delay_secs, args.front()))
		return CommandResult::ERROR;
	client.player_control.SetMixRampDelay(delay_secs);

	return CommandResult::OK;
}

CommandResult
handle_replay_gain_mode(Client &client, ConstBuffer<const char *> args)
{
	if (!replay_gain_set_mode_string(args.front())) {
		command_error(client, ACK_ERROR_ARG,
			      "Unrecognized replay gain mode");
		return CommandResult::ERROR;
	}

	client.partition.outputs.SetReplayGainMode(replay_gain_get_real_mode(client.playlist.queue.random));
	return CommandResult::OK;
}

CommandResult
handle_replay_gain_status(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	client_printf(client, "replay_gain_mode: %s\n",
		      replay_gain_get_mode_string());
	return CommandResult::OK;
}
