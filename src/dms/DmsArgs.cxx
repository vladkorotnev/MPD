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
#include "DmsArgs.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "Partition.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Domain.hxx"
#include "util/StringUtil.hxx"
#include "util/CharUtil.hxx"
#include "util/NumberParser.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/Traits.hxx"
#include "fs/FileSystem.hxx"
#include "fs/NarrowPath.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/io/FileReader.hxx"
#include "Log.hxx"
#include "command/AllCommands.hxx"
#include "Instance.hxx"
#include "Partition.hxx"
#include "storage/CompositeStorage.hxx"
#include "IOThread.hxx"
#include "storage/Registry.hxx"
#include "storage/FileInfo.hxx"
#include "neighbor/Glue.hxx"
#include "neighbor/Info.hxx"
#include "neighbor/Explorer.hxx"
#include "system/Clock.hxx"
#include "sticker/StickerDatabase.hxx"

#include <sstream>

static constexpr Domain dms_args_domain("dms_args");

#ifndef ARRAYSIZE
#define ARRAYSIZE(a)		(int)(sizeof(a)/sizeof(a[0]))
#endif

gcc_pure
static bool
skip_path(const char *name_utf8)
{
	return strchr(name_utf8, '\n') != nullptr;
}

static constexpr bool
IsSafeChar(char ch)
{
	return IsAlphaNumericASCII(ch) || ch == '-' || ch == '_' || ch == '%';
}

static constexpr bool
IsUnsafeChar(char ch)
{
	return !IsSafeChar(ch);
}

static constexpr bool
IsSlash(char ch)
{
	return ch=='/';
}

CommandResult
handle_mount(Client &client, ConstBuffer<const char *> args);

enum DMS_RATE get_master_rate(const AudioFormat af)
{
	enum DMS_RATE rate = RATE_NONE;

	switch (af.format) {
	case SampleFormat::DOP64:
		rate = RATE_DSD64;
		break;
	case SampleFormat::DOP128:
		rate = RATE_DSD128;
		break;
	case SampleFormat::DOP256:
		rate = RATE_DSD256;
		break;
	case SampleFormat::DOP512:
		rate = RATE_DSD512;
		break;
	case SampleFormat::DSD:
		switch (af.sample_rate) {
		case 352800:
			rate = RATE_DSD64;
			break;
		case 705600:
			rate = RATE_DSD128;
			break;
		case 705600*2:
			rate = RATE_DSD256;
			break;
		case 705600*4:
			rate = RATE_DSD512;
			break;
		default:
			break;
		}
		break;
	default:
		if (af.sample_rate <= 44100) {
			rate = RATE_44K1;
		} else if (af.sample_rate <= 48000) {
			rate = RATE_48K;
		} else if (af.sample_rate <= 88200) {
			rate = RATE_88K2;
		} else if (af.sample_rate <= 96000) {
			rate = RATE_96K;
		} else if (af.sample_rate <= 176400) {
			rate = RATE_176K4;
		} else if (af.sample_rate <= 192000) {
			rate = RATE_192K;
		} else if (af.sample_rate <= 352800) {
			rate = RATE_352K8;
		} else if (af.sample_rate <= 384000) {
			rate = RATE_384K;
		} else if (af.sample_rate <= 705600) {
			rate = RATE_705K6;
		} else if (af.sample_rate <= 768000) {
			rate = RATE_768K;
		}
		break;
	}

	return rate;
}

std::string master_rate_tostring(const AudioFormat af)
{
	std::string rate = std::string("NONE");

	switch (af.format) {
	case SampleFormat::DOP64:
		rate = std::string("DSD64");
		break;
	case SampleFormat::DOP128:
		rate = std::string("DSD128");
		break;
	case SampleFormat::DOP256:
		rate = std::string("DSD256");
		break;
	case SampleFormat::DOP512:
		rate = std::string("DSD512");
		break;
	case SampleFormat::DSD:
		switch (af.sample_rate) {
		case 352800:
			rate = std::string("DSD64");
			break;
		case 705600:
			rate = std::string("DSD128");
			break;
		case 705600*2:
			rate = std::string("DSD256");
			break;
		case 705600*4:
			rate = std::string("DSD512");
			break;
		default:
			break;
		}
		break;
	default:
	{
		char c[20];
		if (af.sample_rate % 1000) {
			sprintf(c, "%.1f", af.sample_rate/1000.0);
		} else {
			sprintf(c, "%u", af.sample_rate/1000);
		}
		rate = std::string(c);
		
		break;
	}
	}

	return rate;
}

DmsRate get_current_rate(Client &client)
{
	DmsSource source = client.partition.df.source;
	DmsRate rate;

	if (source.isMaster()) {
		const auto player_status = client.player_control.GetStatus();
		if (player_status.state != PlayerState::STOP &&
			player_status.audio_format.IsDefined()) {
			rate.parse(get_master_rate(player_status.audio_format));
		} else {
			rate.parse(RATE_NONE);
		}
	} else {
		rate = client.partition.df.rate;
	}
	return rate;
}

std::string current_rate_tostring(Client &client)
{
	DmsConfig df = client.partition.df;
	DmsSource source = df.source;
	std::string rate;

	if (source.isMaster()) {
		const auto player_status = client.player_control.GetStatus();
		if (player_status.state != PlayerState::STOP &&
			player_status.audio_format.IsDefined()) {
			rate = master_rate_tostring(player_status.audio_format);
		} else {
			rate = std::string("NONE");
		}
	} else {
		rate = client.partition.df.rate.toString();
	}
	return rate;
}

std::string DmsSource::toString() const
{
	return uri;
}

void DmsSource::reset()
{
	value = SOURCE_NONE;
	uri = "none";
	source_name.clear();
	icon.clear();
}

std::string DmsSource::validSources()
{
	std::string str;

	str.append(validUsbUris());
	str.append(validExternalUris());
	str.append(validNetworkUris());
	str.append(validRendererUris());
	str.append(validSourceScreenUris());
	str.append(validInternetUris());

	return str;
}

std::string DmsSource::validStartups()
{
	std::string str;

	str.append(validUsbUris());
	str.append(validExternalUris());
	str.append(validNetworkUris());
	str.append(validSourceScreenUris());
	str.append(validInternetUris());
	str.append(validLastUsedUris());

	return str;
}

bool DmsSource::parse(const char *s)
{
	if (s == nullptr)
		return false;

	if (parseUsb(s) ||
		parseExternal(s) ||
		parseNetwork(s) ||
		parseRenderer(s) ||
		parseInternet(s) ||
		parseSourceScreen(s)) {
		return true;
	}

	return false;
}

bool DmsSource::parse(channel_t channel)
{
	switch (channel) {
	case CHANNEL_MASTER:
		if (!isMaster()) {
			value = SOURCE_NONE;
		}
		break;
	case CHANNEL_OPTICAL:
		value = SOURCE_OPTICAL;
		break;
	case CHANNEL_COAXIAL_1:
		value = SOURCE_COAXIAL_1;
		break;
	case CHANNEL_COAXIAL_2:
		value = SOURCE_COAXIAL_2;
		break;
	case CHANNEL_AESEBU:
		value = SOURCE_AESEBU;
		break;
	case CHANNEL_BLUETOOTH:
		value = SOURCE_BLUETOOTH;
		break;
	default:
		return false;
	}

	return true;
}

static const char *usb_uri_tbl[] = {
	"sd",
	"usb1",
	"usb2",
	"usb3",
};
static std::string usb_name_tbl[] = {
	"SD",
	"USB1 (Front)",
	"USB2 (Rear)",
	"USB3 (Rear)",
};
std::string DmsSource::validUsbUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(usb_uri_tbl);i++) {
		uris.append(usb_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}
bool DmsSource::parseUsb(const char *s)
{
	if (s == nullptr)
		return false;

	for (int i=0;i<ARRAYSIZE(usb_uri_tbl);i++) {
		if (strcasecmp(s, usb_uri_tbl[i]) == 0) {
			value = (enum DMS_SOURCE)(i + SOURCE_SD);
			uri = usb_uri_tbl[i];
			source_name = usb_name_tbl[i];
			return true;
		}
	}

	return false;
}

static const char *external_uri_tbl[] = {
	"coaxial1",
	"coaxial2",
	"optical",
	"aesebu",
	"bluetooth",
};
static std::string external_name_tbl[] = {
	"Coaxial 1",
	"Coaxial 2",
	"Optical",
	"AES/EBU",
	"Bluetooth",
};
void
dms_source_file_write(BufferedOutputStream &os)
{
	for(int i=0;i<ARRAYSIZE(external_uri_tbl);i++) {
		os.Format("source: %s\n", external_uri_tbl[i]);
		os.Format("source_name: %s\n", external_name_tbl[i].c_str());
	}
}

std::string DmsSource::validExternalUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(external_uri_tbl);i++) {
		uris.append(external_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}

bool DmsSource::parseExternal(const char *s)
{
	if (s == nullptr)
		return false;

	for (int i=0;i<ARRAYSIZE(external_uri_tbl);i++) {
		if (strcasecmp(s, external_uri_tbl[i]) == 0) {
			value = (enum DMS_SOURCE)(i + SOURCE_COAXIAL_1);
			uri = external_uri_tbl[i];
			source_name = external_name_tbl[i];
			return true;
		}
	}

	return false;
}

static const char *network_uri_tbl[] = {
	"smb://",
	"upnp://",
	"phone://",
};
static std::string network_name_tbl[] = {
	"PC Share",
	"Media Server",
	"Phone",
};
std::string DmsSource::validNetworkUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(network_uri_tbl);i++) {
		uris.append(network_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}

bool DmsSource::parseNetwork(const char *s)
{
	if (s == nullptr)
		return false;

	for (int i=0;i<ARRAYSIZE(network_uri_tbl);i++) {
		if (StringStartsWith(s, network_uri_tbl[i])) {
			value = (enum DMS_SOURCE)(i + SOURCE_SMB);
			uri = s;
			if (value == SOURCE_PHONE) {
				source_name = s + strlen(network_uri_tbl[i]);
			} else {
				source_name = network_name_tbl[i];
			}
			return true;
		}
	}

	return false;
}

static const char *renderer_uri_tbl[] = {
	"renderer://DLNA",
	"renderer://Airplay",
	"renderer://RoonReady",
};
static std::string renderer_name_tbl[] = {
	"DLNA",
	"AirPlay",
	"Roon Ready",
};
std::string DmsSource::validRendererUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(renderer_uri_tbl);i++) {
		uris.append(renderer_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}

bool DmsSource::parseRenderer(const char *s)
{
	if (s == nullptr)
		return false;

	for (int i=0;i<ARRAYSIZE(renderer_uri_tbl);i++) {
		if (strcasecmp(s, renderer_uri_tbl[i]) == 0) {
			value = (enum DMS_SOURCE)(i + SOURCE_DLNA);
			uri = renderer_uri_tbl[i];
			source_name = renderer_name_tbl[i];
			return true;
		}
	}

	return false;
}

static const char *source_screen_uri_tbl[] = {
	"usb_source",
	"network_source",
	"internet_source",
};
static std::string source_screen_name_tbl[] = {
	"USB (Main)",
	"Network (Main)",
	"Internet (Main)",
};
std::string DmsSource::validSourceScreenUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(source_screen_uri_tbl);i++) {
		uris.append(source_screen_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}

bool DmsSource::parseSourceScreen(const char *s)
{
	if (s == nullptr)
		return false;

	for (int i=0;i<ARRAYSIZE(source_screen_uri_tbl);i++) {
		if (strcasecmp(s, source_screen_uri_tbl[i]) == 0) {
			value = (enum DMS_SOURCE)(i + SOURCE_USB_SOURCE);
			uri = source_screen_uri_tbl[i];
			source_name = source_screen_name_tbl[i];
			return true;
		}
	}

	return false;
}

static const char *internet_uri_tbl[] = {
	"internet://spotify",
	"internet://tidal",
	"internet://vtuner",
};
static std::string internet_name_tbl[] = {
	"Spotify",
	"TIDAL",
	"vTuner"
};

bool
DmsSource::isTidal() const
{
	return uri.compare(internet_uri_tbl[1]) == 0;
}

bool
DmsSource::isSpotify() const
{
	return uri.compare(internet_uri_tbl[0]) == 0;
}

std::string DmsSource::validInternetUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(internet_uri_tbl);i++) {
		uris.append(internet_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}

bool DmsSource::parseInternet(const char *s)
{
	if (s == nullptr)
		return false;

	for (int i=0;i<ARRAYSIZE(internet_uri_tbl);i++) {
		if (strcasecmp(s, internet_uri_tbl[i]) == 0) {
			value = SOURCE_INTERNET;
			uri = internet_uri_tbl[i];
			source_name = internet_name_tbl[i];
			return true;
		}
	}

	return false;
}

static const char *last_used_uri_tbl[] = {
	"network_last_used",
	"intertnet_last_used",
};
static std::string last_used_name_tbl[] = {
	"Network (Last Used)",
	"Internet (Last Used)",
};
std::string DmsSource::validLastUsedUris()
{
	std::string uris;

	for (int i=0;i<ARRAYSIZE(last_used_uri_tbl);i++) {
		uris.append(last_used_uri_tbl[i]);
		uris.append(" ");
	}

	return uris;
}

bool DmsSource::parseStartup(const char *s)
{
	if (s == nullptr)
		return false;

	if (parseUsb(s) ||
		parseExternal(s) ||
		parseNetwork(s) ||
		parseInternet(s) ||
		parseSourceScreen(s)) {
		return true;
	}
	for (int i=0;i<ARRAYSIZE(last_used_uri_tbl);i++) {
		if (strcasecmp(s, last_used_uri_tbl[i]) == 0) {
			value = (source_t)(i + SOURCE_NETWORK_LAST_USED);
			uri = last_used_uri_tbl[i];
			source_name = last_used_name_tbl[i];
			return true;
		}
	}

	return false;
}

bool DmsSource::isMaster() const
{
	if (isUsb() ||
		isNetwork() ||
		isRenderer() ||
		isInternet()) {
		return true;
	} else {
		return false;
	}
}

bool DmsSource::isUsb() const
{
	switch (value) {
	case SOURCE_SD:
	case SOURCE_USB_1:
	case SOURCE_USB_2:
	case SOURCE_USB_3:
		return true;

	default:
		return false;
	}
}

bool DmsSource::isNetwork() const
{
	switch (value) {
	case SOURCE_SMB:
	case SOURCE_UPNP:
	case SOURCE_PHONE:
		return true;

	default:
		return false;
	}
}

bool DmsSource::isExternal() const
{
	switch (value) {
	case SOURCE_COAXIAL_1:
	case SOURCE_COAXIAL_2:
	case SOURCE_OPTICAL:
	case SOURCE_BLUETOOTH:
	case SOURCE_AESEBU:
		return true;

	default:
		return false;
	}
}

bool DmsSource::isRenderer() const
{
	switch (value) {
	case SOURCE_DLNA:
	case SOURCE_AIRPLAY:
	case SOURCE_ROONREADY:
		return true;

	default:
		return false;
	}
}

channel_t DmsSource::toChannel() const
{
	if (isMaster()) {
		return CHANNEL_MASTER;
	}
	switch (value) {
	case SOURCE_OPTICAL:
		return CHANNEL_OPTICAL;
	case SOURCE_COAXIAL_1:
		return CHANNEL_COAXIAL_1;
	case SOURCE_COAXIAL_2:
		return CHANNEL_COAXIAL_2;
	case SOURCE_AESEBU:
		return CHANNEL_AESEBU;
	case SOURCE_BLUETOOTH:
		return CHANNEL_BLUETOOTH;

	default:
		return CHANNEL_MASTER;
	}
}

bool getAllMounts(Storage *_composite, std::list<std::string> &list)
{
	if (_composite == nullptr) {
		return false;
	}

	CompositeStorage &composite = *(CompositeStorage *)_composite;

	list.clear();
	const auto visitor = [&list](const char *mount_uri,
				       gcc_unused const Storage &storage){
		if (*mount_uri != 0) {
			list.push_back(mount_uri);
		}
	};

	composite.VisitMounts(visitor);
	
	return true;
}

bool DmsSource::isSourceScreen() const
{
	switch (value) {
	case SOURCE_USB_SOURCE:
	case SOURCE_NETWORK_SOURCE:
	case SOURCE_INTERNET_SOURCE:
		return true;
	default:
		return false;
	}
}

CommandResult DmsSource::mountUsb(Client &client, DmsUsb &_usb)
{
	std::vector<std::string> mlist = _usb.ids;
	std::vector<std::string> plist = _usb.paths;
	CommandResult ret = CommandResult::ERROR;

	for (unsigned i=0;i<mlist.size()&&i<plist.size();i++) {
		std::string m = mlist[i];
		std::string p = plist[i];
		const char *argv[2];
		ConstBuffer<const char *> args(argv, 0);
		argv[args.size++] = m.c_str();
		argv[args.size++] = p.c_str();
		if (handle_mount(client, args) == CommandResult::OK) {
			ret = CommandResult::OK;
		}
	}
	
	return ret;
}

CommandResult DmsSource::mountSamba(Client &client, ConstBuffer<const char *> _args)
{
	CommandResult ret = CommandResult::ERROR;
	
	std::string local_prefix(_args[0] + strlen("smb://"));
	local_prefix.append("_");

	std::string remote_prefix(_args[0]);	
	remote_prefix.append("/");

	const char *url = _args[0];
	Error error;
	Storage *storage = CreateStorageURI(io_thread_get(), remote_prefix.c_str(), error);
	if (storage == nullptr) {
		if (error.IsDefined()) {
			ret = print_error(client, error);
			FormatError(dms_args_domain,"%s %d %s", __func__, __LINE__, error.GetMessage());
			return ret;
		}

		command_error(client, ACK_ERROR_ARG,
			      "Unrecognized storage URI");
		FormatError(dms_args_domain,"%s %d %s", __func__, __LINE__, "Unrecognized storage URI");
		return CommandResult::ERROR;
	}
	int try_cnt = 5;
	StorageDirectoryReader *reader = nullptr;
	do {
		reader = storage->OpenDirectory("", error);
		if (reader != nullptr)
			break;
		FormatDefault(dms_args_domain,"list smb file fail, try again?!\n");
	} while (--try_cnt);
	if (reader == nullptr) {
		ret = print_error(client, error);
		FormatError(dms_args_domain,"%s %d %s", __func__, __LINE__, error.GetMessage());
		return ret;
	}

	const char *name_utf8 = nullptr;
	std::vector<std::string> name_utf8_list;
	std::vector<StorageFileInfo> info_list;
	reader->Lock();
	while (1) {
		int try_cnt2 = 3;
		do {
			name_utf8 = reader->Read();
			if (name_utf8 != nullptr)
				break;
		} while (--try_cnt2);
		if (name_utf8 == nullptr) {
			break;
		}
		if (skip_path(name_utf8))
			continue;
		
		StorageFileInfo info;
		if (!reader->GetInfo(false, info, error))
			continue;

		name_utf8_list.push_back(std::string(name_utf8));
		info_list.push_back(info);
	}
	reader->Unlock();
	
	delete reader;
	delete storage;
	for(size_t i=0;i<name_utf8_list.size();i++) {
		std::string local_uri;
		std::string remote_uri(remote_prefix);
		const char *argv[2];
		ConstBuffer<const char *> args(argv, 0);
		switch (info_list[i].type) {
		case StorageFileInfo::Type::OTHER:
		case StorageFileInfo::Type::REGULAR:
			/* ignore */
			continue;

		case StorageFileInfo::Type::DIRECTORY:
			if (!StringEndsWith(name_utf8_list[i].c_str(), "$")) {
				local_uri.append(name_utf8_list[i].c_str());
				remote_uri.append(name_utf8_list[i].c_str());
				argv[args.size++] = local_uri.c_str();
				argv[args.size++] = remote_uri.c_str();
				FormatDefault(dms_args_domain, "mount %s %s",local_uri.c_str(), remote_uri.c_str());
				CommandResult ret2 = handle_mount(client, args);
				ret = ret == CommandResult::OK ? ret : ret2;
				if (ret2 != CommandResult::OK) {
					FormatDefault(dms_args_domain,"mount %s error\n", remote_uri.c_str());
				}
			}
			break;
		}
	}
	
	if (name_utf8_list.empty()) {
		ret = CommandResult::OK;
	}
	return ret;
}

std::string DmsSource::getQueuePath()
{
	if (uri.empty())
		return uri;
	std::string str;
	std::string id;
	switch (value) {
	case SOURCE_UPNP:
		str.append("upnp_");
		id = uri.substr(strlen("upnp://uuid"), 35);
		str.append(id);
		break;
	case SOURCE_SD:
	case SOURCE_USB_1:
	case SOURCE_USB_2:
	case SOURCE_USB_3:
		if (usb.ids.size()>0) {
			str.append(usb.ids.front());
		} else {
			str.append(toString());
		}
		break;
	default:
		str.append(uri);
		break;
	}
	std::replace_if(str.begin(), str.end(), IsUnsafeChar, '_');
	return str;
}

std::string DmsSource::getPlaylistName(const char *playlist)
{
	std::string str = getQueuePath();
	if (playlist == nullptr || playlist[0] == '\0' || str.empty()) {
		return std::string();
	}
	str.append("_");
	str.append(playlist);
	return str;
}

bool DmsSource::checkMounted(Client &client)
{
	std::list<std::string> list;
	Storage *_composite = client.partition.instance.storage;
	if (!getAllMounts(_composite, list)) {
		return false;
	}
	return isMounted(list);
}

bool DmsSource::isMounted(std::list<std::string> &list)
{
	switch (value) {
	case SOURCE_SD:
	case SOURCE_USB_1:
	case SOURCE_USB_2:
	case SOURCE_USB_3:
		for(auto str:list) {
			if (strcmp(str.c_str(), toString().c_str()) == 0) {
				return true;
			}
		}
		break;
	case SOURCE_COAXIAL_1:
	case SOURCE_COAXIAL_2:
	case SOURCE_OPTICAL:
	case SOURCE_AESEBU:
	case SOURCE_BLUETOOTH:
		return true;
	default:
		break;
	}
	return false;
}

void printAvailableSource(Client &client)
{
	client_printf(client, "inputs: USB/SD\n");
	for(int i=0;i<ARRAYSIZE(usb_uri_tbl);i++) {
		client_printf(client, "source: %s\n", usb_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", usb_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: DAC\n");
	for(int i=0;i<ARRAYSIZE(external_uri_tbl);i++) {
		client_printf(client, "source: %s\n", external_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", external_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: NETWORK\n");
	for(int i=0;i<ARRAYSIZE(network_uri_tbl);i++) {
		client_printf(client, "source: %s\n", network_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", network_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: INTERNET\n");
	for(int i=0;i<ARRAYSIZE(internet_uri_tbl);i++) {
		client_printf(client, "source: %s\n", internet_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", internet_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: RENDERER\n");
	for(int i=0;i<ARRAYSIZE(renderer_uri_tbl);i++) {
		client_printf(client, "source: %s\n", renderer_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", renderer_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: SOURCE SCREEN\n");
	for(int i=0;i<ARRAYSIZE(source_screen_uri_tbl);i++) {
		client_printf(client, "source: %s\n", source_screen_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", source_screen_name_tbl[i].c_str());
	}
}

void printAvailableStartup(Client &client)
{
	client_printf(client, "inputs: USB&SD\n");
	for(int i=0;i<ARRAYSIZE(usb_uri_tbl);i++) {
		client_printf(client, "source: %s\n", usb_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", usb_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: DAC\n");
	for(int i=0;i<ARRAYSIZE(external_uri_tbl);i++) {
		client_printf(client, "source: %s\n", external_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", external_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: NETWORK\n");
	for(int i=0;i<ARRAYSIZE(network_uri_tbl);i++) {
		client_printf(client, "source: %s\n", network_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", network_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: INTERNET\n");
	for(int i=0;i<ARRAYSIZE(internet_uri_tbl);i++) {
		client_printf(client, "source: %s\n", internet_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", internet_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: SOURCE SCREEN\n");
	for(int i=0;i<ARRAYSIZE(source_screen_uri_tbl);i++) {
		client_printf(client, "source: %s\n", source_screen_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", source_screen_name_tbl[i].c_str());
	}

	client_printf(client, "inputs: LAST USED\n");
	for(int i=0;i<ARRAYSIZE(last_used_uri_tbl);i++) {
		client_printf(client, "source: %s\n", last_used_uri_tbl[i]);
		client_printf(client, "source_name: %s\n", last_used_name_tbl[i].c_str());
	}
}

bool renameSourceName(Client &client, const char *s, const char *n)
{
	assert(s != nullptr);
	assert(s != nullptr);

	for(int i=0;i<ARRAYSIZE(external_uri_tbl);i++) {
		if (strcasecmp(s, external_uri_tbl[i]) == 0) {
			external_name_tbl[i] = n;
			return true;
		}
	}

	for(int i=0;i<ARRAYSIZE(usb_uri_tbl);i++) {
		if (strcasecmp(s, usb_uri_tbl[i]) == 0) {
			goto found;
		}
	}

	for(int i=0;i<ARRAYSIZE(network_uri_tbl);i++) {
		if (strcasecmp(s, network_uri_tbl[i]) == 0) {
			goto found;
		}
	}
	for(int i=0;i<ARRAYSIZE(internet_uri_tbl);i++) {
		if (strcasecmp(s, internet_uri_tbl[i]) == 0) {
			goto found;
		}
	}
	for(int i=0;i<ARRAYSIZE(renderer_uri_tbl);i++) {
		if (strcasecmp(s, renderer_uri_tbl[i]) == 0) {
			goto found;
		}
	}
	for(int i=0;i<ARRAYSIZE(source_screen_uri_tbl);i++) {
		if (strcasecmp(s, source_screen_uri_tbl[i]) == 0) {
			goto found;
		}
	}
	command_error(client, ACK_ERROR_ARG,
			  "%s is not a supported source", s);

	return false;

found:
	command_error(client, ACK_ERROR_ARG,
			  "%s not support rename", s);
	return false;
}

bool renameSourceName(DmsSource s, const char *n)
{
	if (s.isExternal()) {
		external_name_tbl[s.value - SOURCE_COAXIAL_1] = n;
		return true;
	}

	return false;
}

const char *DmsBluetooth::arg_state_tbl[] = {
	"Pairing",
	"Pairing",
	"Connecting",
	"Connected",
};

const char *DmsBluetooth::arg_codec_tbl[] = {
	"",
	"SBC",
	"AAC",
	"apt-X",
	"apt-X LL",
};

std::string DmsBluetooth::stateString() const
{
	return (state >= ARRAYSIZE(arg_state_tbl) ? "" : arg_state_tbl[(int)state]);
}

std::string DmsBluetooth::codecString() const
{
	return (codec >= ARRAYSIZE(arg_codec_tbl) ? "" : arg_codec_tbl[(int)codec]);
}

bool DmsBluetooth::parse(enum STATE _state)
{
	switch (_state) {
	case PREPARE:
		state = PAIRING;
		break;
	case PAIRING:
	case CONNECTING:
	case CONNECTED:
		state = (enum STATE)_state;
		break;
	default:
		return false;
	}
	return true;
}

bool DmsBluetooth::parse(enum CODEC _codec)
{
	if (_codec < CODEC_END) {
		codec = (enum CODEC)_codec;
		return true;
	}
	return false;
}

player_status
DmsBluetooth::getStatus()
{
	status.elapsed_time = SongTime::FromMS(MonotonicClockMS() - start_time_point);
	status.bufferd_time = status.elapsed_time;

	return status;
}

const char *DmsVolumePolicy::arg_name_tbl[] = {
	"master",
	"independent",
};

std::string DmsVolumePolicy::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsVolumePolicy::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	return str;
}

bool DmsVolumePolicy::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			value = (enum DMS_VOLUME_POLICY)i;
			return true;
		}
	}

	return false;
}

const char *DmsIr::arg_name_tbl[] = {
	"front",
	"rear",
	"both",
};

std::string DmsIr::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsIr::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	return str;
}

bool DmsIr::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			value = (enum DMS_IR)i;
			return true;
		}
	}

	return false;
}

bool DmsIr::parse(bool front, bool rear)
{
	if (front && rear) {
		value = IR_BOTH;
	} else if (front && !rear) {
		value = IR_FRONT;
	} else if (!front && rear) {
		value = IR_REAR;
	} else {
		value = IR_DISABLE;
	}
	return true;
}

const char *DmsNetWorkType::arg_name_tbl[] = {
	"ethernet",
	"wifi",
};

std::string DmsNetWorkType::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsNetWorkType::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	return str;
}

bool DmsNetWorkType::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			value = (enum DMS_NETWORK)i;
			return true;
		}
	}

	return false;
}

const char *DmsIpAssign::arg_name_tbl[] = {
	"dhcp",
	"static",
};

std::string DmsIpAssign::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsIpAssign::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	return str;
}

bool DmsIpAssign::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			value = (enum DMS_IP_ASSIGN)i;
			return true;
		}
	}

	return false;
}

const char *DmsBrightness::arg_name_tbl[] = {
	"off",
	"low",
	"middle",
	"high",
	"up",
	"down",
};
const unsigned char DmsBrightness::arg_value_tbl[] = {
	(unsigned char)(0	* 2.554),
	(unsigned char)(35	* 2.554),
	(unsigned char)(65	* 2.554),
	(unsigned char)(100	* 2.554),
};

std::string DmsBrightness::toString() const
{
	enum DMS_BRIGHTNESS level = toLevel();
	return (level >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[level]);
}

std::string DmsBrightness::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	str.append("[0,100]");
	
	return str;
}

enum DMS_BRIGHTNESS DmsBrightness::toLevel(unsigned char v) const
{
	enum DMS_BRIGHTNESS level = BRIGHTNESS_OFF;
	if (v > arg_value_tbl[BRIGHTNESS_OFF])
		level = BRIGHTNESS_LOW;
	if (v >= arg_value_tbl[BRIGHTNESS_MIDDLE])
		level = BRIGHTNESS_MIDDLE;
	if (v >= arg_value_tbl[BRIGHTNESS_HIGH])
		level = BRIGHTNESS_HIGH;
	return level;

}

/* for read from kernel */
bool DmsBrightness::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	unsigned long v;
	char *endptr;

	v = strtoul(s, &endptr, 10);
	if (!(endptr == s ) && v <= 255) {
		value = v ;
		return true;
	}

	return false;
}

bool DmsBrightness::parse(const char *s, const DmsBrightness &cur)
{

	if (s == nullptr)
		return false;
	
	unsigned char current_value = cur.value;
	enum DMS_BRIGHTNESS level = toLevel(current_value);
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			if (i<=BRIGHTNESS_HIGH) {
				level = (enum DMS_BRIGHTNESS)i;
			} else {
				switch (i-4) {
				case DMS_UP:
					level = level == BRIGHTNESS_HIGH ? BRIGHTNESS_OFF : (enum DMS_BRIGHTNESS)(level+1);
					break;
				case DMS_DOWN:
					level = level == BRIGHTNESS_OFF ? BRIGHTNESS_HIGH : (enum DMS_BRIGHTNESS)(level-1);
					break;
				default:
					FormatDefault(dms_args_domain, "%s %d", __func__, __LINE__);
					return false;
				}
			}
			value = arg_value_tbl[level];
			return true;
		}
	}

	/* for user */
	unsigned long v;
	char *endptr;

	v = strtoul(s, &endptr, 10);
	if (!(endptr == s ) && v <= 100) {
		value = v * 2.554;
		return true;
	}

	return false;
}

bool DmsBrightness::parse(enum DMS_BRIGHTNESS level)
{
	value = arg_value_tbl[level];
	return true;
}

bool DmsBrightness::parse(unsigned char bri)
{
	value = bri;
	return true;
}

const char *DmsSRC::arg_name_tbl[] = {
	"bypass",
	"44.1",
	"48",
	"88.2",
	"96",
	"176.4",
	"192",
	"352.8",
	"384",
	"705.6",
	"768",
	"DSD64",
	"DSD128",
	"DSD256",
	"DSD512",
	"",
	"up",
};

std::string DmsSRC::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsSRC::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	
	return str;
}

bool DmsSRC::parse(const char *s, const DmsRate &cur_rate, const DmsSRC &cur_SRC)
{
	if (s == nullptr)
		return false;
	
	enum DMS_RATE current_rate = cur_rate.value;
	enum DMS_RATE current_SRC = cur_SRC.value;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			if (i == RATE_UP) {
				DMS_RATE src;
				switch (current_rate) {
				case RATE_NONE:
					if (current_SRC == RATE_DSD512) {
						src = RATE_DSD512;
					} else {
						src = current_SRC == RATE_MAX ? RATE_BYPASS : (DMS_RATE)(current_SRC+1);
					}
					break;
				case RATE_44K1:
				case RATE_88K2:
				case RATE_176K4:
				case RATE_352K8:
				case RATE_705K6:
					src = current_SRC == RATE_MAX ? RATE_BYPASS : (DMS_RATE)(current_SRC+1);
					if (src != RATE_BYPASS && src <= current_rate) {
						src = current_rate == RATE_MAX ? RATE_MAX : (DMS_RATE)(current_rate+1);
					}
					if (src == RATE_384K) {
						src = RATE_705K6;
					} else if (src == RATE_768K) {
						src = RATE_DSD64;
					}
					break;
				case RATE_48K:
				case RATE_96K:
				case RATE_192K:
				case RATE_384K:
				case RATE_768K:
					src = current_SRC == RATE_MAX ? RATE_BYPASS : (DMS_RATE)(current_SRC+1);
					if (src != RATE_BYPASS && src <= current_rate) {
						src = current_rate == RATE_MAX ? RATE_MAX : (DMS_RATE)(current_rate+1);
					}
					if (src == RATE_352K8) {
						src = RATE_384K;
					} else if (src == RATE_705K6) {
						src = RATE_768K;
					}
					break;
				case RATE_DSD64:
				case RATE_DSD128:
				case RATE_DSD256:
					src = current_SRC == RATE_MAX ? RATE_BYPASS : (DMS_RATE)(current_SRC+1);
					if (src != RATE_BYPASS && src <= current_rate) {
						src = current_rate == RATE_MAX ? RATE_MAX : (DMS_RATE)(current_rate+1);
					}
					break;
				case RATE_DSD512:
					src = RATE_DSD512;
					break;
				default:
					fprintf(stderr, "error, unknown rate: %d", current_rate);
					src = RATE_BYPASS;
					break;
				}
				value = src;
			} else {
				value = (enum DMS_RATE)i;
			}
			return true;
		}
	}

	return false;
}

bool DmsSRC::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strncasecmp(s, arg_name_tbl[i], strlen(arg_name_tbl[i])) == 0) {
			value = (enum DMS_RATE)i;
			return true;
		}
	}

	return false;
}

bool DmsSRC::parse(enum DMS_RATE src)
{
	if (src >= RATE_BYPASS
		&& src <= RATE_MAX) {
		value = src;
		return true;
	}

	return false;
}

void
DmsSRC::apply() const
{
	unsigned v = value;

	writeFile("/sys/dms-m3/SRC", &v, 1);
}

void
DmsSRC::load(const char *uri)
{
	std::string str = sticker_load_value("misc", uri, "src", IgnoreError());
	if (!str.empty()) {
		parse(str.c_str());
	}
}

void
DmsSRC::store(const char *uri) const
{
	sticker_store_value("misc", uri, "src", arg_name_tbl[(int)value], IgnoreError());
}

static const uint32_t samplerate_tbl[] = {
	0,
	44100,
	48000,
	88200,
	96000,
	176400,
	192000,
	352800,
	384000,
	705600,
	768000,
	44100*64,
	44100*128,
	44100*256,
	44100*512,
	0,
};

DmsSRC DmsSRC::fromUint(unsigned int r)
{
	DmsSRC rt;

	for (unsigned i=ARRAYSIZE(samplerate_tbl)-1;i>0;i--) {
		if (r == samplerate_tbl[i]) {
			rt.value = (DMS_RATE)i;
			break;
		}
	}

	return rt;
}

const char *DmsRate::arg_name_tbl[] = {
	"",
	"44.1",
	"48",
	"88.2",
	"96",
	"176.4",
	"192",
	"352.8",
	"384",
	"705.6",
	"768",
	"DSD64",
	"DSD128",
	"DSD256",
	"DSD512",
	"NONE",
};

std::string DmsRate::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

uint32_t DmsRate::toUint() const
{
	return samplerate_tbl[value];
}

std::string DmsRate::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		if (str.empty())
			continue;
		str.append(s);
		str.append(" ");
	}
	
	return str;
}

bool DmsRate::parse(enum DMS_RATE rate)
{
	if ((rate >= RATE_44K1 && rate <= RATE_DSD512)
		|| rate == RATE_NONE) {
		value = rate;
		return true;
	}

	return false;
}

DmsRate DmsRate::fromUint(unsigned int r)
{
	DmsRate rt;

	for (unsigned i=1;i<ARRAYSIZE(samplerate_tbl);i++) {
		if (r == samplerate_tbl[i]) {
			rt.value = (DMS_RATE)i;
			break;
		}
	}

	return rt;
}

const char *DmsVolume::arg_name_tbl[] = {
	"up",
	"down",
};

static const char *volume_perventage_action[] = {
	"up_centi",
	"down_centi",
};

std::string DmsVolume::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsVolume::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	for (auto s:volume_perventage_action) {
		str.append(s);
		str.append(" ");
	}
	str.append("[0,100] ");
	str.append("[-98.5db,8db]");
	
	return str;
}

void apply_volume(double vol)
{
	if (vol > DMS_VOLUME_MAX_DB) {
		vol = DMS_VOLUME_MAX_DB;
	} else if (vol < DMS_VOLUME_MIN_DB) {
		vol = DMS_VOLUME_MIN_DB;
	}
	unsigned char regvol = volume_db_to_value(vol);
	writeFile("/sys/dms-m3/volume", &regvol, sizeof(regvol));
}

double load_volume(const char *uri, double def)
{
	std::string str = sticker_load_value("misc", uri, "volume", IgnoreError());
	if (str.empty()) {
		return def;
	}

	return ParseDouble(str.c_str());
}

void store_volume(const char *uri, double vol)
{
	std::stringstream stream;
	stream << vol;
	sticker_store_value("misc", uri, "volume", stream.str().c_str(), IgnoreError());
}

bool DmsVolume::parse(const char *s, const DmsVolume &cur_vol, bool cur_mute)
{
	if (s == nullptr)
		return false;

	unsigned char current_vol = cur_vol.value;

	// up/down db/step
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			switch (i) {
			case DMS_UP:
				value = current_vol >= DMS_VOLUME_MAX ? DMS_VOLUME_MAX : current_vol+1;
				if (cur_mute) {
					muteAction = MUTE_ACTION_UNMUTE;
				}
				break;
			case DMS_DOWN:
				if (cur_vol.value == DMS_VOLUME_MIN) {
					muteAction = MUTE_ACTION_MUTE;
				} else	if (cur_mute) {
					muteAction = MUTE_ACTION_UNMUTE;
				}
				value = current_vol <= DMS_VOLUME_MIN ? DMS_VOLUME_MIN : current_vol-1;
				break;
			default:
				return false;
			}
			return true;
		}
	}

	// up/down percentage/step
	for (int i=0;i<ARRAYSIZE(volume_perventage_action);i++) {
		if (strcasecmp(s, volume_perventage_action[i]) == 0) {
			unsigned char cur_per = cur_vol.toPercentage();
			switch (i) {
			case DMS_UP:
				cur_per = cur_per >= 100 ? 100 : (cur_per + 1);
				fromPercentage(cur_per);
				if (cur_mute) {
					muteAction = MUTE_ACTION_UNMUTE;
				}
				break;
			case DMS_DOWN:
				if (cur_vol.value == DMS_VOLUME_MIN) {
					muteAction = MUTE_ACTION_MUTE;
				} else	if (cur_mute) {
					muteAction = MUTE_ACTION_UNMUTE;
				}
				cur_per = cur_per == 0 ? 0 : (cur_per - 1);
				fromPercentage(cur_per);
				break;
			default:
				return false;
			}
			return true;
		}
	}

	// [-98.5db,8db]
	if (StringEndsWith(s, "db")) {
		const char *dbptr = strchr(s, 'd');
		float db;
		char *endptr;
		
		db = strtof(s, &endptr);
		FormatDefault(dms_args_domain, "%s %d %f %p %p", __func__, __LINE__, db, s, endptr);
		if (endptr != dbptr ||
			db > DMS_VOLUME_MAX_DB ||
			db < DMS_VOLUME_MIN_DB) {
			return false;
		}
		if (cur_mute) {
			muteAction = MUTE_ACTION_UNMUTE;
		}
		value = volume_db_to_value(db);
		return true;
	}


	// [0,100]%
	unsigned long percentage;
	char *endptr;
	percentage = strtoul(s, &endptr, 10);
	if (endptr == s || *endptr != 0 || percentage > 100) {
		return false;
	}
	if (cur_mute) {
		muteAction = MUTE_ACTION_UNMUTE;
	}
	value = volume_perventage_to_value(percentage);

	return true;
}

bool DmsVolume::parse(unsigned char vol)
{
	if (vol >= DMS_VOLUME_MIN) {
		value = vol;
		return true;
	}
	return false;
}

unsigned char DmsVolume::toPercentage() const
{
	return ((value-DMS_VOLUME_MIN)*100.0/(DMS_VOLUME_MAX-DMS_VOLUME_MIN) + 0.5);
}

void DmsVolume::fromPercentage(unsigned char per)
{
	value = (DMS_VOLUME_MAX - DMS_VOLUME_MIN)/100.0 * per + DMS_VOLUME_MIN;
}

float DmsVolume::toDb()
{
	return (DMS_VOLUME_MIN_DB + 0.5*(value-DMS_VOLUME_MIN));
}

const char *DmsBool::arg_name_tbl[] = {
	"0",
	"1",
	"2",
	
	"off",
	"on",
	"toggle",
	
	"disable",
	"enable",
	"toggle",
	
	"false",
	"true",
	"toggle",
};

std::string DmsBool::toString() const
{
	return (value >= ARRAYSIZE(arg_name_tbl) ? "" : arg_name_tbl[(int)value]);
}

std::string DmsBool::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	
	return str;
}

bool DmsBool::parse(const char *s, const DmsBool &cur_bool)
{
	if (s == nullptr)
		return false;

	bool current_bool = cur_bool.value;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			while(i >= 3) i -= 3;
			if (i == DMS_TOGGLE)
				i = !current_bool;
			value = (enum DMS_ON_OFF_TOGGLE)i;
			return true;
		}
	}
	
	return false;
}

bool DmsBool::parse(const char *s)
{
	if (s == nullptr)
		return false;
	
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			while(i >= 3) i -= 3;
			if (i == DMS_TOGGLE)
				i = 0;
			value = (enum DMS_ON_OFF_TOGGLE)i;
			return true;
		}
	}
	
	return false;
}

bool DmsBool::parse(bool on)
{
	value = (enum DMS_ON_OFF_TOGGLE)on;
	return true;
}


int DmsPort::toInt()
{
	return value;
}

std::string DmsPort::validArgs()
{
	std::string str;

	str.append("[1000,65535]");
	
	return str;
}

bool DmsPort::parse(const char *s)
{
	if (s == nullptr)
		return false;

	unsigned long port;
	char *endptr;
	port = strtoul(s, &endptr, 10);
	if (endptr == s || *endptr != 0 || port < 1000) {
		return false;
	}
	value = port;
	
	return true;
}

std::string DmsIp::toString() const
{
	return value;
}

std::string DmsIp::validArgs()
{
	std::string str;

	str.append("192.168.[0,255].[0,255]");
	
	return str;
}

bool DmsIp::parse(const char *s)
{
	char strIp[100];
	
	if (s == nullptr)
		return false;

	int ip[4];
	int n = sscanf(s, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
	if (n == 4) {
		for (int i=0;i<4;i++) {
			if (!(ip[i]>=0 && ip[i]<=255)) {
				return false;
			}
		}
	} else {
		return false;
	}
	memset(strIp, 0, sizeof(strIp));
	sprintf(strIp, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	value = std::string(strIp);
	
	return true;
}

std::string DmsVersion::toString() const
{
	return value;
}

bool DmsVersion::parse(const char *version)
{
	value = std::string(version);
	return true;
}

const char *DmsUsb::arg_name_tbl[] = {
	"sd",
	"usb1",
	"usb2",
	"usb3",
};

const char *DmsUsb::path_tbl[] = {
	"/mnt/external_sd",
	"/mnt/usb_storage/USB_DISK0",
	"/mnt/usb_storage/USB_DISK1",
	"/mnt/usb_storage/USB_DISK2",
};

std::string DmsUsb::toString() const
{
	switch (value) {
	case SOURCE_SD:
		return std::string(arg_name_tbl[0]);
	case SOURCE_USB_1:
		return std::string(arg_name_tbl[1]);
	case SOURCE_USB_2:
		return std::string(arg_name_tbl[2]);
	case SOURCE_USB_3:
		return std::string(arg_name_tbl[3]);
	default:
		return std::string("");
	}
}

std::string DmsUsb::getPath() const
{
	switch (value) {
	case SOURCE_SD:
		return std::string(path_tbl[0]);
	case SOURCE_USB_1:
		return std::string(path_tbl[1]);
	case SOURCE_USB_2:
		return std::string(path_tbl[2]);
	case SOURCE_USB_3:
		return std::string(path_tbl[3]);
	default:
		return std::string("");
	}
}

bool DmsUpdate::parse(unsigned char v)
{
	if (v < UPDATE_END) {
		value = v;
		return true;
	}
	
	return false;
}

const char *DmsPoweron::arg_name_tbl[] = {
	"off",
	"on",
	"startup",
	"ok",
	"offing",
};

bool DmsPoweron::parse(unsigned char v)
{
	if (v < POWERON_END) {
		value = v;
		return true;
	}
	
	return false;
}

bool DmsPoweron::parse(const char *s)
{
	for (int i=0;i<ARRAYSIZE(arg_name_tbl);i++) {
		if (strcasecmp(s, arg_name_tbl[i]) == 0) {
			value = i;
			return true;
		}
	}
	return false;
}

std::string DmsPoweron::toString() const
{
	if (value >= POWERON_END)
		return std::string();

	return arg_name_tbl[value];
}

std::string DmsPoweron::validArgs()
{
	std::string str;

	for (auto s:arg_name_tbl) {
		str.append(s);
		str.append(" ");
	}
	
	return str;
}

const char *CoverOption::arg_order_tbl[] = {
	"song",
	"folder",
	"internet",
};

std::string CoverOption::validArgs()
{
	std::string str = "order: ";

	for (auto s:arg_order_tbl) {
		str.append(s);
		str.append(" ");
	}
	
	return str;
}

CoverOption::CoverOption()
{
	for(int i=0;i<ARRAYSIZE(orders);i++) {
		orders[i] = (TYPE)i;
	}
}

bool CoverOption::parse(const char *s, TYPE &type)
{
	for (int i=0;i<ARRAYSIZE(arg_order_tbl);i++) {
		if (strcasecmp(s, arg_order_tbl[i]) == 0) {
			type = (TYPE)i;
			return true;
		}
	}
	return false;
}

bool CoverOption::parse(ConstBuffer<const char *> args)
{
	TYPE od[COVER_MAX];
	int pos = 0;
	memset(od, -1, sizeof(od));
	for (size_t i=0;i<args.size&&i<COVER_MAX;i++) {
		TYPE type;
		if (!parse(args[i], type))
			return false;
		od[pos++] = type;
	}
	memcpy(orders, od, sizeof(orders));
	return true;
}

bool CoverOption::parseOrders(std::vector<std::string> ol)
{
	TYPE od[COVER_MAX];
	int pos = 0;

	memset(od, -1, sizeof(od));
	for (size_t i=0;i<ol.size()&&i<COVER_MAX;i++) {
		TYPE type;
		if (!parse(ol[i].c_str(), type))
			return false;
		od[pos++] = type;
	}
	memcpy(orders, od, sizeof(orders));

	return true;
}

std::vector<std::string> CoverOption::toOrderStringList() const
{
	std::vector<std::string> list;
	
	for(int i=0;i<ARRAYSIZE(orders);i++) {
		if (orders[i] != -1) {
			list.push_back(std::string(arg_order_tbl[orders[i]]));
		}
	}
	
	return list;
}

static const char *dsdtype_tbl[] = {
	"dsd2pcm",
	"dop",
	"native",
};

std::string
DSDType::toString() const
{
	assert(value < ARRAYSIZE(dsdtype_tbl));

	return dsdtype_tbl[value];
}

bool
DSDType::parse(const char *s)
{
	for (int i=0;i<ARRAYSIZE(dsdtype_tbl);i++) {
		if (strcasecmp(s, dsdtype_tbl[i]) == 0) {
			value = i;
			return true;
		}
	}
	return false;
}
