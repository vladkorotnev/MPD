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
#include "DmsCommands.hxx"
#include "NeighborCommands.hxx"
#include "CommandError.hxx"
#include "client/Client.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Domain.hxx"
#include "util/StringUtil.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/Traits.hxx"
#include "fs/FileSystem.hxx"
#include "fs/NarrowPath.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/io/FileReader.hxx"
#include "storage/CompositeStorage.hxx"
#include "storage/Registry.hxx"
#include "neighbor/Glue.hxx"
#include "neighbor/Info.hxx"
#include "Log.hxx"
#include "Instance.hxx"
#include "Partition.hxx"
#include "Idle.hxx"
#include "BulkEdit.hxx"
#include "SongLoader.hxx"
#include "playlist/PlaylistQueue.hxx"
#include "PlaylistSave.hxx"
#include "PlaylistFile.hxx"
#include "dms/DmsStateFile.hxx"
#include "IOThread.hxx"
#include "db/plugins/simple/SimpleDatabasePlugin.hxx"
#include "StateFile.hxx"
#include "db/update/Service.hxx"
#include "dms/DmsQueueFile.hxx"
#include "Stats.hxx"
#include "command/QueueCommands.hxx"
#include "LogBackend.hxx"
#include "input/plugins/AlsaInputPlugin.hxx"
#include "dms/DmsSourceFile.hxx"
#include "dms/Context.hxx"
#include "dms/util/IOUtil.hxx"
#include "command/PlayerCommands.hxx"
#include "dms/Usb.hxx"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
static constexpr Domain dms_domain("dms_command");

extern Instance *instance;

extern StateFile *state_file;
extern DmsStateFile *dms_state_file;
extern DmsQueueFile *dms_queue_file;

extern CommandResult
handle_unmount(Client &client, ConstBuffer<const char *> args);

static bool
unmount(const char *local_uri)
{
	Storage *_composite = instance->storage;
	if (_composite == nullptr) {
		return false;
	}

	CompositeStorage &composite = *(CompositeStorage *)_composite;

	if (*local_uri == 0) {
		return false;
	}

#ifdef ENABLE_DATABASE
	if (instance->update != nullptr)
		/* ensure that no database update will attempt to work
		   with the database/storage instances we're about to
		   destroy here */
		instance->update->CancelMount(local_uri);

	Database *_db = instance->database;
	if (_db != nullptr && _db->IsPlugin(simple_db_plugin)) {
		SimpleDatabase &db = *(SimpleDatabase *)_db;

		if (db.Unmount(local_uri))
			// TODO: call Instance::OnDatabaseModified()?
			idle_add(IDLE_DATABASE);
	}
#endif

	if (!composite.Unmount(local_uri)) {
		return false;
	}

	idle_add(IDLE_MOUNT);

	return true;
}

bool
unmountAll()
{
	Storage *_composite = instance->storage;
	if (_composite == nullptr) {
		return false;
	}

	std::list<std::string> list;
	if (getAllMounts(_composite, list)) {
		for (auto str : list) {
			unmount(str.c_str());
		}
	}
	
	return true;
}

static CommandResult changeDmsSourceTo(Client &client, DmsSource &source)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	DmsSRC src, amendSrc, changedSrc;
	DmsVolume volume, changedVolume, preVolume = df.getVolume();
	DmsSource changedSource, preSource = df.source;
	AudioFormat format = df.in_format;

	if (source.isSourceScreen()) {
		dc.writeSource(source.value, df.getSRC(source).value);
		usleep(150);
		dc.readSource(changedSource, df.rate, changedSrc);
	} else {
		amendSrc = src = df.getSRC(source);
		if (df.source.isMaster()) {
			amendSrc = amendDSDSRC(amendSrc, format);
		}
		dc.writeSource(source.value, amendSrc.value);
		usleep(150);
		dc.writeVolume(df.getVolume(source).value);
		usleep(2);
		client.context.applyMute(client.context.mute);
		usleep(2);
		dc.writeBypass(false);
		usleep(2);

		// check write
		dc.readSource(changedSource, df.rate, changedSrc);
		usleep(2);
		dc.readVolume(changedVolume);
		usleep(2);
		usleep(2);
		if (source.value == SOURCE_BLUETOOTH) {
			if (!dc.readBluetoothStatus(df.bluetooth)) {
				FormatDefault(dms_domain, "read bluetooth status fail %s %s",
					df.bluetooth.stateString().c_str(), df.bluetooth.codecString().c_str());
				command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
				FormatError(dms_domain, DMS_IO_ERROR);
				return CommandResult::ERROR;
			}
			usleep(2);
		}
	}

	// save config and notify
	if (source != preSource) {
		switch (source.value) {
		case SOURCE_SMB:
		case SOURCE_UPNP:
			df.networkLastUri = source.uri;
			break;
		case SOURCE_INTERNET:
			df.internetLastUri = source.uri;
			break;
		case SOURCE_PHONE:
		default:
			break;
		}
		df.source = source;
		idle_add(IDLE_DMS_SOURCE);
	}
	if (df.src != src) {
		df.src = src;
		idle_add(IDLE_DMS_SRC);
	}
	if (df.getVolume() != preVolume) {
		idle_add(IDLE_MIXER);
	}

	return CommandResult::OK;
}

static bool saveSourceQueue(Client &client, DmsSource &source)
{
	DmsControl	&dc = client.partition.dc;
	std::string sourceQueue = source.getQueuePath();

	if (source.isMaster() && !source.isSourceScreen()
		&& source.value != SOURCE_PHONE) {
		dc.saveQueue(sourceQueue);
	}
	return true;
}

static bool loadSourceQueue(Client &client, DmsSource &source)
{
	DmsControl	&dc = client.partition.dc;
	std::string sourceQueue = source.getQueuePath();

	dc.clearQueuePath();
	client.partition.ClearQueue();
	if (source.isMaster()
		&& !source.isSourceScreen()
		&& !source.isRenderer()
		&& !source.isInternet()
		&& source.value != SOURCE_PHONE) {
		dc.loadQueue(sourceQueue);
	} else if (source.isInternet()) {
		dc.stopQueueSchedule();
	}
	return true;
}

CommandResult
handle_loadSourceQueue(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource	&source = df.source;
	DmsControl	&dc = client.partition.dc;
	std::string sourceQueue = source.getQueuePath();

	dc.clearQueuePath();
	client.partition.ClearQueue();
	dc.loadQueue(sourceQueue);

	return CommandResult::OK;
}

static std::string getSourceNameByUri(Client &client, std::string uri)
{
	const NeighborGlue *const neighbors =
		client.partition.instance.neighbors;
	if (neighbors == nullptr) {
		return std::string();
	}
	
	for (const auto &i : neighbors->GetList()) {
		if (i.uri.compare(uri) == 0) {
			FormatDefault(dms_domain, "name=%s", i.display_name.c_str());
			return i.display_name;
		}
	}
	return std::string();
}

static std::string getSourceIconByUri(Client &client, std::string uri)
{
	const NeighborGlue *const neighbors =
		client.partition.instance.neighbors;
	if (neighbors == nullptr) {
		return std::string();
	}
	
	for (const auto &i : neighbors->GetList()) {
		if (i.uri.compare(uri) == 0) {
			return i.device_icon_url;
		}
	}
	return std::string();
}

static void
changePlaymode(Client &client, const DmsSource &source, const Dms::Playmode &playmode)
{
	Dms::Playmode p;

	if (source.isDLNA()) {
		p.mode = Dms::Playmode::SEQUENCE;
		FormatDefault(dms_domain, "dlna so change playmode to sequence");
	} else {
		p = playmode;
	}
	playmode.apply(client.partition);
}

CommandResult
handle_dmsUsb(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsControl	&dc = client.partition.dc;
	DmsSource source;
	std::list<DmsUsb> usbs;
	DmsUsb usb;
	CommandResult ret;

	if (!source.parseUsb(args[0])) {
		command_error(client, ACK_ERROR_ARG,
				  "%s expected:%s",
				  source.validUsbUris().c_str(), args[0]);
		return CommandResult::ERROR;
	}
	dc.readUsbs(usbs);
	for (auto &_usb : usbs) {
		if (_usb.value == source.value) {
			source.usb = usb = _usb;
			break;
		}
	}
	if (!usb.valid()) {
		command_error(client, ACK_ERROR_ARG,
				  "no device found");
		FormatError(dms_domain, "no device found");
		return CommandResult::ERROR;
	}
	handle_unmountAll(client, args);
	ret = source.mountUsb(client, usb);
	if (ret != CommandResult::OK) {
		command_error(client, ACK_ERROR_NO_EXIST, "mount failed!");
		return ret;
	}
	saveSourceQueue(client, df.source);
	ret = changeDmsSourceTo(client, source);
	if (ret != CommandResult::OK) {
		return ret;
	}
	changePlaymode(client, source, client.context.playmode);
	loadSourceQueue(client, source);

	return CommandResult::OK;
}

CommandResult
handle_dmsExternal(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource source;
	CommandResult ret;
	
	if (!source.parseExternal(args[0])) {
		command_error(client, ACK_ERROR_ARG,
				  "%s expected:%s",
				  source.validExternalUris().c_str(), args[0]);
		return CommandResult::ERROR;
	}
	handle_unmountAll(client, args);
	saveSourceQueue(client, df.source);
	ret = changeDmsSourceTo(client, source);
	if (ret != CommandResult::OK) {
		return ret;
	}
	changePlaymode(client, source, client.context.playmode);
	loadSourceQueue(client, source);

	return CommandResult::OK;
}

CommandResult
handle_dmsSourceScreen(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource source;
	CommandResult ret;

	if (!source.parseSourceScreen(args[0])) {
		command_error(client, ACK_ERROR_ARG,
				  "%s expected:%s",
				  source.validSourceScreenUris().c_str(), args[0]);
		return CommandResult::ERROR;
	}
	handle_unmountAll(client, args);
	saveSourceQueue(client, df.source);
	ret = changeDmsSourceTo(client, source);
	if (ret != CommandResult::OK) {
		return ret;
	}
	changePlaymode(client, source, client.context.playmode);
	loadSourceQueue(client, source);

	return ret;
}

CommandResult
handle_dmsInternet(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource source;
	CommandResult ret;
	Error error;

	if (!source.parseInternet(args[0])) {
		command_error(client, ACK_ERROR_ARG,
				  "%s expected:%s",
				  source.validInternetUris().c_str(), args[0]);
		return CommandResult::ERROR;
	}
	handle_unmountAll(client, args);
	saveSourceQueue(client, df.source);
	ret = changeDmsSourceTo(client, source);
	if (ret != CommandResult::OK) {
		return ret;
	}
	changePlaymode(client, source, client.context.playmode);
	loadSourceQueue(client, source);

	return CommandResult::OK;
}

CommandResult
handle_dmsRenderer(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource source;
	CommandResult ret;

	FormatDefault(dms_domain, "%s %s", __func__, args.front());
	if (!source.parseRenderer(args[0])) {
		command_error(client, ACK_ERROR_ARG,
				  "%s expected:%s",
				  source.validRendererUris().c_str(), args[0]);
		return CommandResult::ERROR;
	}
	handle_unmountAll(client, args);
	saveSourceQueue(client, df.source);
	ret = changeDmsSourceTo(client, source);
	changePlaymode(client, source, client.context.playmode);
	loadSourceQueue(client, source);
	if (source.isAirplay()) {
		const char *_argv[1];
		ConstBuffer<const char *> _args(_argv, 0);
		_argv[_args.size++] = "airplay://airplay";
		ret = handle_add(client, _args);
		if (ret != CommandResult::OK) {
			return ret;
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsNetworkSource(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource source;
	std::string sever_name;

	if (!source.parseNetwork(args.front())) {
		command_error(client, ACK_ERROR_ARG,
			      "%s  expected: %s",
			      source.validNetworkUris().c_str(), args.front());
		return CommandResult::ERROR;
	}

	if (source.isSamba() || source.isUpnp()) {
		sever_name = getSourceNameByUri(client, source.uri);
		if (sever_name.empty()) {
			FormatError(dms_domain, "can't find server(%s) name", source.toString().c_str());
			command_error(client, ACK_ERROR_NO_EXIST,
					  "can't find server(%s) name",
					  source.toString().c_str());
			return CommandResult::ERROR;
		}
	}
	handle_unmountAll(client, args);
	CommandResult ret;
	if (source.isSamba()) {
		ret = source.mountSamba(client,args);
		if (ret != CommandResult::OK) {
			return ret;
		}
		source.source_name = sever_name;
	} else if (source.isUpnp()) {
		Database *upnpdatabase = client.partition.instance.upnpdatabase;
		if (upnpdatabase != nullptr) {
			upnpdatabase->Close();
			Error error;
			if (!upnpdatabase->Open(error)) {
				FormatError(dms_domain, "open upnp error:%s", error.GetMessage());
				command_error(client, ACK_ERROR_SYSTEM,
						  "open upnp error:%s",
						  error.GetMessage());
				return CommandResult::ERROR;
			}
		}
		source.icon = getSourceIconByUri(client, source.getUri());
		source.source_name = sever_name;
	}

	saveSourceQueue(client, df.source);
	ret = changeDmsSourceTo(client, source);
	if (ret != CommandResult::OK) {
		return ret;
	}
	changePlaymode(client, source, client.context.playmode);
	loadSourceQueue(client, source);

	return CommandResult::OK;
}

CommandResult
handle_dmssource(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource source, preSource = df.source;
	const char *_argv[10];
	ConstBuffer<const char *> _args(_argv, 0);
	CommandResult ret = CommandResult::ERROR;

	if (args.IsEmpty()) {
		client_printf(client, "source: %s\n", df.source.uri.c_str());
		client_printf(client, "source_name: %s\n", df.source.source_name.c_str());
		return CommandResult::OK;
	} else {
		if (!source.parse(args[0])) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  source.validSources().c_str(), args[0]);
			FormatDefault(dms_domain, "%s expected:%s",
				source.validSources().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		FormatDefault(dms_domain, "source: %s -> %s",
			df.source.uri.c_str(), source.uri.c_str());

		usleep(2);
		if (preSource.isMaster()
			&& client.player_control.GetStatus().state == PlayerState::PLAY) {
			client.player_control.Pause();
			usleep(10);
		}

		if (source.isUsb()) {
			ret = handle_dmsUsb(client, args);
		} else if (source.isExternal()) {
			ret = handle_dmsExternal(client, args);
		} else if (source.isNetwork()) {
			ret = handle_dmsNetworkSource(client, args);
		} else if (source.isSourceScreen()) {
			ret = handle_dmsSourceScreen(client, args);
		} else if (source.isInternet()) {
			ret = handle_dmsInternet(client, args);
		} else if (source.isRenderer()) {
			ret = handle_dmsRenderer(client, args);
		}
		
		stats_invalidate();
		if (ret == CommandResult::OK) {
			client.context.applyMute(client.context.mute);
			//dc.writeTube(df.tube.value);
			client.context.poweron.parse(Dms::Poweron::OK);
			client.context.poweron.apply();
			client.context.poweron.acquire();
		}
	}

	return ret;
}

CommandResult
handle_setvol(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	DmsVolume volume, changedVolume, preVolume = df.getVolume();

	if (!volume.parse(args[0], preVolume, client.context.mute)) {
		command_error(client, ACK_ERROR_ARG,
				  "%s expected:%s",
				  volume.validArgs().c_str(), args[0]);
		return CommandResult::ERROR;
	}
	if (!dc.writeVolume(volume.value)) {
		command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
		return CommandResult::ERROR;
	}
	if (volume.muteAction == DmsVolume::MUTE_ACTION_MUTE
		|| volume.muteAction == DmsVolume::MUTE_ACTION_UNMUTE) {
		client.context.applyMute(volume.muteAction == DmsVolume::MUTE_ACTION_MUTE);
	}
	if (!(dc.readVolume(changedVolume)
		&& changedVolume == volume)) {
		command_error(client, ACK_ERROR_DMS_IO, DMS_CHECK_WRITE_BACK_ERROR);
		return CommandResult::ERROR;
	}
	if (volume != preVolume
		|| volume.muteAction == DmsVolume::MUTE_ACTION_MUTE
		|| volume.muteAction == DmsVolume::MUTE_ACTION_UNMUTE) {
		df.getVolume() = volume;
		idle_add(IDLE_MIXER);
		if (volume.muteAction == DmsVolume::MUTE_ACTION_MUTE
			|| volume.muteAction == DmsVolume::MUTE_ACTION_UNMUTE) {
			if (volume.muteAction == DmsVolume::MUTE_ACTION_MUTE) {
				client.context.mute = DMS_ON;
			} else {
				client.context.mute = DMS_OFF;
			}
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsvolume(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsVolume volume;
	
	if (args.IsEmpty()) {
		if (!dc.readVolume(volume)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			FormatError(dms_domain, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		client_printf(client,
			"volume_db: %.1f\n"
			"volume: %i\n",
			volume.toDb(),
			volume.toPercentage());
	} else {
		return handle_setvol(client, args);
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsvolumepolicy(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	DmsVolumePolicy policy;
	DmsVolume changedVolume, preVolume = df.getVolume();
	
	if (args.IsEmpty()) {
		client_printf(client,
			"volume_policy: %s\n",
			df.volume_policy.toString().c_str());
	} else {
		if (!policy.parse(args[0])) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  policy.validArgs().c_str(), args[0]);
			FormatError(dms_domain, "%s expected:%s",
					  policy.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}

		df.volume_policy = policy;
		if (!dc.writeVolume(df.getVolume().getValue())) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			FormatError(dms_domain, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		if (!(dc.readVolume(changedVolume)
			&& changedVolume == df.getVolume())) {
			FormatError(dms_domain, "volume write(0x%x) != read(0x%x)",
				df.getVolume().value, changedVolume.value);
			command_error(client, ACK_ERROR_DMS_IO, DMS_CHECK_WRITE_BACK_ERROR);
			FormatError(dms_domain, DMS_CHECK_WRITE_BACK_ERROR);
			return CommandResult::ERROR;
		}
		if (df.getVolume() != preVolume) {
			idle_add(IDLE_MIXER);
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsversion(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsVersion version;

	args = args;
	if (!dc.readVersion(version)) {
		command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
		FormatError(dms_domain, DMS_IO_ERROR);
		return CommandResult::ERROR;
	}
	client_printf(client, "version: %s\n", version.toString().c_str());

	return CommandResult::OK;
}

CommandResult
handle_dmsSRC(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	DmsSRC src, amendSrc, changedSrc, preSrc = df.getSRC();
	AudioFormat format = df.in_format;
	
	if (args.IsEmpty()) {
		if (!dc.readSRC(src)) {
			FormatError(dms_domain, DMS_IO_ERROR);
		} else {
			FormatDefault(dms_domain, "src: %s", src.toString().c_str());
		}
		client_printf(client, "SRC: %s\n", preSrc.toString().c_str());
	} else {
		DmsRate rate = get_current_rate(client);
		if (rate == RATE_DSD512) {
			return CommandResult::OK;
		}
		if (!src.parse(args[0], rate, df.src)) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  src.validArgs().c_str(), args[0]);
			FormatError(dms_domain, "%s expected:%s",
					  src.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}

		amendSrc = src;
		if (df.source.isMaster()) {
			amendSrc = amendDSDSRC(amendSrc, format);
		}
		if (!dc.writeSRC(df.source.value, amendSrc.value)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			FormatError(dms_domain, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		FormatDefault(dms_domain,"SRC(%s) -> (%s)", 
			preSrc.toString().c_str(), src.toString().c_str());
		if (src != df.src
			|| preSrc != src) {
			df.src = src;
			df.getSRC() = src;
			idle_add(IDLE_DMS_SRC);
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsRate(Client &client, ConstBuffer<const char *> args)
{	
	DmsControl	&dc = client.partition.dc;
	DmsSource source = client.partition.df.source;
	DmsRate rate;
	std::string strRate;
	args = args;
	
	if (source.isMaster()) {
		const auto player_status = client.player_control.GetStatus();
		if (player_status.audio_format.IsDefined()) {
			strRate = master_rate_tostring(player_status.audio_format);
		} else {
			strRate = std::string("NONE");
		}
		client_printf(client, "rate: %s\n", strRate.c_str());
	} else {
		if (!dc.readRate(rate)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			FormatError(dms_domain, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		client_printf(client, "rate: %s\n", rate.toString().c_str());
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsStartup(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsSource startup;

	if (args.IsEmpty()) {
		client_printf(client, "startup: %s\n", df.startup.uri.c_str());
	} else {
		if (!startup.parseStartup(args.front())) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  startup.validStartups().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		df.startup = startup;
		if (dms_state_file != nullptr) {
			dms_state_file->Write();
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsIr(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	DmsIr	ir, changedIr, preIr = df.ir;

	if (args.IsEmpty()) {
		if (!dc.readIr(ir)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			FormatError(dms_domain, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		client_printf(client, "ir: %s\n", ir.toString().c_str());
	} else {
		if (!ir.parse(args.front())) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  ir.validArgs().c_str(), args[0]);
			FormatError(dms_domain, "%s expected:%s",
					  ir.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		
		if (!dc.writeIr(ir.value)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			FormatError(dms_domain, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		if (!(dc.readIr(changedIr)
			&& changedIr == ir)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_CHECK_WRITE_BACK_ERROR);
			FormatError(dms_domain, DMS_CHECK_WRITE_BACK_ERROR);
			return CommandResult::ERROR;
		}

		if (ir != preIr) {
			df.ir = ir;
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsNetwork(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsNetWorkType type;
	DmsIpAssign assign;
	DmsPort port;
	DmsIp ip;

	if (args.IsEmpty()) {
		client_printf(client, "network_type: %s\n"
			"ip_assign: %s\n"
			"port: %u\n"
			"static_ip: %s\n",
			df.networkType.toString().c_str(),
			df.ipAssign.toString().c_str(),
			df.port.value,
			df.staticIp.toString().c_str());
	} else {
		switch (args.size) {
		case 4:
			if (!port.parse(args[2])) {
				command_error(client, ACK_ERROR_ARG,
						  "%s expected:%s",
						  port.validArgs().c_str(), args[2]);
				FormatError(dms_domain, "%s expected:%s",
						  port.validArgs().c_str(), args[2]);
				return CommandResult::ERROR;
			}
			/*  don't change ip if ip is "0.0.0.0" */
			if (strcmp(args[3], "0.0.0.0")!=0 && !ip.parse(args[3])) {
				command_error(client, ACK_ERROR_ARG,
						  "%s expected:%s",
						  ip.validArgs().c_str(), args[3]);
				FormatError(dms_domain, "%s expected:%s",
						  ip.validArgs().c_str(), args[3]);
				return CommandResult::ERROR;
			}
			// fall throuth
			
		case 2:
			if (!type.parse(args[0])) {
				command_error(client, ACK_ERROR_ARG,
						  "%s expected:%s",
						  type.validArgs().c_str(), args[0]);
				FormatError(dms_domain, "%s expected:%s",
						  type.validArgs().c_str(), args[0]);
				return CommandResult::ERROR;
			}
			if (!assign.parse(args[1])) {
				command_error(client, ACK_ERROR_ARG,
						  "%s expected:%s",
						  assign.validArgs().c_str(), args[1]);
				FormatError(dms_domain, "%s expected:%s",
						  assign.validArgs().c_str(), args[1]);
				return CommandResult::ERROR;
			}
			if (args.size == 2 && assign.value == IP_ASSIGN_STATIC) {
				command_error(client, ACK_ERROR_ARG,
						  "network_type {static} port static_ip expected:");
				FormatError(dms_domain, "network_type {static} port static_ip expected:");
				return CommandResult::ERROR;
			}
			break;
		default:
			command_error(client, ACK_ERROR_ARG,
					  "network_type ip_assign [port] [static_ip] expected");
			FormatError(dms_domain, "network_type ip_assign [port] [static_ip] expected");
			return CommandResult::ERROR;
		}

		switch (args.size) {
		case 4:
			df.port = port;
			df.staticIp = ip;
			// fall throuth
			
		case 2:
			df.networkType = type;
			df.ipAssign = assign;
			break;
		default:
			assert(false);
			gcc_unreachable();
		}
		dms_state_file->Write();
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsUserTips(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsControl	&dc = client.partition.dc;
	DmsBool	usertips;

	if (args.IsEmpty()) {
		client_printf(client, "user_tips: %u\n", df.usertips.toBool());
	} else {
		if (!usertips.parse(args[0], df.usertips)) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  usertips.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		if (!dc.writeUserTips(usertips.toBool())) {
			FormatDefault(dms_domain, "write usertips %s",DMS_IO_ERROR);
		}
		if (df.usertips != usertips) {
			df.usertips = usertips;
			idle_add(IDLE_USERTIPS);
			dms_state_file->Write();
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsAppUserTips(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsBool	appUsertips;

	if (args.IsEmpty()) {
		client_printf(client, "app_user_tips: %u\n", df.appUsertips.toBool());
	} else {
		if (!appUsertips.parse(args[0], df.appUsertips)) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  appUsertips.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		if (df.appUsertips != appUsertips) {
			df.appUsertips = appUsertips;
			idle_add(IDLE_USERTIPS);
			dms_state_file->Write();
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsBrightness(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	DmsBrightness bri, changedBri, preBri = df.brightness;

	if (args.IsEmpty()) {
		if (!dc.readBrightness(bri)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		client_printf(client,
			"brightness: %u\n"
			"brightness_level: %s\n",
			bri.toPercentage(),
			bri.toString().c_str());
	} else {
		if (!bri.parse(args[0], preBri)) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  bri.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		if (!dc.writeBrightness(bri.value)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		//dc.writeBacklightPower(bri.value > 0);
		if (!(dc.readBrightness(changedBri)
			&& changedBri == bri)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_CHECK_WRITE_BACK_ERROR);
			return CommandResult::ERROR;
		}
		df.brightness = bri;
		dms_state_file->Write();
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsMute(Client &client, ConstBuffer<const char *> args)
{
	if (args.IsEmpty()) {
		client_printf(client, "mute: %u\n", client.context.mute);
	} else {
		if (!check_bool(client, &client.context.mute, args.front())) {
			return CommandResult::ERROR;
		}
		client.context.apply(Dms::Context::MUTE);
		client.context.store(Dms::Context::MUTE);
		idle_add(IDLE_MIXER);
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsHmute(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsBool mute;

	if (args.IsEmpty()) {
		if (!dc.readPopMute(mute)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
		client_printf(client, "hmute: %u\n", mute.toBool());
	} else {
		if (!mute.parse(args[0])) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  mute.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		if (!dc.writeHMute(mute.value)) {
			command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
			return CommandResult::ERROR;
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_listLocals(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	Dms::UsbStorage usbs = client.context.usbmonitor.LockGet();

	for (const auto &i : usbs.list) {
		switch (i.toSource()) {
		case Dms::SOURCE_SD:
			client_printf(client, "local: sd\n");
			break;
		case Dms::SOURCE_USB_1:
			client_printf(client, "local: usb1\n");
			break;
		case Dms::SOURCE_USB_2:
			client_printf(client, "local: usb2\n");
			break;
		case Dms::SOURCE_USB_3:
			client_printf(client, "local: usb3\n");
			break;
		default:
			break;
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_unmountAll(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	Storage *_composite = client.partition.instance.storage;
	if (_composite == nullptr) {
		command_error(client, ACK_ERROR_NO_EXIST, "No database");
		return CommandResult::ERROR;
	}
	UpdateService *update = client.partition.instance.update;
	if (update != nullptr) {
		update->CancelAllAsync();
	}

	std::list<std::string> list;
	if (getAllMounts(_composite, list)) {
		const char *_argv[1];
		ConstBuffer<const char *> _args(_argv, 1);
		for (auto str : list) {
			_argv[0] = str.c_str();
			handle_unmount(client, _args);
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_playmode(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;

	if (args.IsEmpty()) {
		client_printf(client, "playmode: %s\n", client.context.playmode.c_str());
	} else {
		if (df.source.isDLNA()) {
			command_error(client, ACK_ERROR_ARG,
					  "not support this command in DLNA now!");
			return CommandResult::ERROR;
		}
		Dms::Playmode playmode;
		if (!playmode.parse(args.front())) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  playmode.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		playmode.apply(client.partition);
		client.context.playmode = playmode;
		client.context.playmode.store();
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsUpdate(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;

	if (client.player_control.GetStatus().state == PlayerState::PLAY) {
		client.player_control.Pause();
	}
	if (!dc.writeUpdate(args.front(), strlen(args.front()))) {
		command_error(client, ACK_ERROR_DMS_IO, "Update failed!");
		return CommandResult::ERROR;
	}
	FormatDefault(dms_domain, "%s len=%d", __func__, strlen(args.front()));
  	
	return CommandResult::OK;
}

CommandResult
handle_poweron(Client &client, ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsConfig	&df = client.partition.df;
	Dms::Poweron poweron;

	if (args.IsEmpty()) {
		client.context.poweron.acquire();
		client_printf(client, "poweron: %s\n", client.context.poweron.c_str());
	} else {
		poweron.parse(args[0]);
		if (poweron.isOff() ||
			poweron.isOffing()) {
			if (client.player_control.GetStatus().state == PlayerState::PLAY) {
				DmsControl::writePopMute(true);
				client.partition.Stop();
			}
			if (instance->update != nullptr) {
				instance->update->CancelAllAsync();
			}
			if (state_file != nullptr) {
				state_file->Write();
			}
			if (dms_state_file != nullptr) {
				dms_state_file->Write();
			}
#ifndef CLEAN_ALL_QUEUE_WHEN_POWEROFF
			if (dms_queue_file != nullptr) {
				dms_queue_file->Write();
			}
#endif
			handle_unmountAll(client, args);
			client.partition.ClearQueue();
			//df.source.reset();
#ifdef CLEAN_ALL_QUEUE_WHEN_POWEROFF
			DmsControl::deleteAllQueueFile();
#endif
			if (poweron.isOffing()) {
				idle_add(IDLE_POWERON);
				client.context.poweron = poweron;
			}
		}
		poweron.apply();

		if (poweron.isOk()) {
			dc.writeSource(df.source.value);
			dc.writeVolume(df.getVolume().value);
			client.context.applyMute(client.context.mute);
			//dc.writeTube(df.tube.value);
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_dmsBluetoothStatus(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	DmsBluetooth bluetooth;

	if (!dc.readBluetoothStatus(bluetooth)) {
		command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
		return CommandResult::ERROR;
	}
	client_printf(client, "bluetooth_state: %s\n", bluetooth.stateString().c_str());
	client_printf(client, "bluetooth_codec: %s\n", bluetooth.codecString().c_str());

	return CommandResult::OK;
}

CommandResult
handle_getSn(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	DmsControl	&dc = client.partition.dc;
	std::string sn;

	if (!dc.readSn(sn)) {
		command_error(client, ACK_ERROR_DMS_IO, DMS_IO_ERROR);
		return CommandResult::ERROR;
	}
	client_printf(client, "sn: %s\n", sn.c_str());

	return CommandResult::OK;
}

CommandResult
handle_aliasName(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;

	if (args.IsEmpty()) {
		client_printf(client, "alias_name: %s\n", df.aliasName.c_str());
	} else {
		std::string name = std::string(args[0]);
		if (name.empty()) {
			command_error(client, ACK_ERROR_ARG, "name shouldnot empty!");
			return CommandResult::ERROR;
		}
		df.aliasName = name;
		if (dms_state_file != nullptr) {
			dms_state_file->Write();
		}
	}

	return CommandResult::OK;
}

CommandResult
handle_neighborOptions(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	DmsBool	pcshare;
	DmsBool	mediaserver;

	if (args.IsEmpty()) {
		client_printf(client, "pcshare: %u\n", df.pcshare.toBool());
		client_printf(client, "mediaserver: %u\n", df.mediaserver.toBool());
	} else {
		if (!pcshare.parse(args[0], df.pcshare)) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected:%s",
					  pcshare.validArgs().c_str(), args[0]);
			return CommandResult::ERROR;
		}
		df.pcshare = pcshare;
		if (args.size > 1) {
			if (!mediaserver.parse(args[1], df.mediaserver)) {
				command_error(client, ACK_ERROR_ARG,
						  "%s expected:%s",
						  mediaserver.validArgs().c_str(), args[0]);
				return CommandResult::ERROR;
			}
			df.mediaserver = mediaserver;
		}
		dms_state_file->Write();
	}

	return CommandResult::OK;
}

CommandResult
handle_coverSource(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	CoverOption	co;

	if (args.IsEmpty()) {
		std::vector<std::string> sl = df.coverOption.toOrderStringList();
		for (auto str : sl) {
			client_printf(client, "source: %s\n", str.c_str());
		}
	} else {
		if (!co.parse(args)) {
			command_error(client, ACK_ERROR_ARG,
					  "%s expected",
					  co.validArgs().c_str());
			return CommandResult::ERROR;
		}
		df.coverOption = co;
		dms_state_file->Write();
	}

	return CommandResult::OK;
}

CommandResult
handle_folderCoverPatterns(Client &client, ConstBuffer<const char *> args)
{
	DmsConfig	&df = client.partition.df;
	FolderCoverOption	fco;

	if (args.IsEmpty()) {
		auto sl = df.folderCoverOption.sl;
		for (auto str : sl) {
			client_printf(client, "pattern: %s\n", str.c_str());
		}
	} else {
		command_error(client, ACK_ERROR_ARG,
				  "can't set yet");
		return CommandResult::ERROR;
	}

	return CommandResult::OK;
}

CommandResult
handle_listSources(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	printAvailableSource(client);
	return CommandResult::OK;
}

CommandResult
handle_listStartups(Client &client, gcc_unused ConstBuffer<const char *> args)
{
	printAvailableStartup(client);
	return CommandResult::OK;
}

CommandResult
handle_renameSourceName(Client &client, ConstBuffer<const char *> args)
{
	const char *s = args[0];
	const char *n = args[1];

	if (s == nullptr || n == nullptr) {
		command_error(client, ACK_ERROR_ARG,
				  "source(%s) or name(%s) is empty", s, n);
		return CommandResult::ERROR;
	}
	bool ret = renameSourceName(client, s, n);
	if (ret) {
		dms_source_file_save();
	}

	return ret ? CommandResult::OK : CommandResult::ERROR;
}

CommandResult
handle_set_config(Client &client, ConstBuffer<const char *> args)
{
	const char *key = args[0];
	const char *value = args[1];

	if (StringIsEqual(key, "now_playing_theme")) {
		client.context.db.store("now_playing_theme", value);
	}

	idle_add(IDLE_CONFIG);

	return CommandResult::OK;
}

CommandResult
handle_list_config(Client &client, ConstBuffer<const char *> args)
{
	const char *key = args.IsEmpty() ? nullptr : args[0];

	if (!key || StringIsEqual(key, "now_playing_theme")) {
		client_printf(client, "now_playing_theme: %s\n", client.context.db.load("now_playing_theme", "normal").c_str());
	}

	return CommandResult::OK;
}

