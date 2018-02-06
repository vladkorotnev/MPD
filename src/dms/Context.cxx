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
#include "dms/Context.hxx"
#include "sticker/StickerDatabase.hxx"
#include "util/NumberParser.hxx"
#include "util/Macros.hxx"
#include "util/Domain.hxx"
#include "util/DivideString.hxx"
#include "util/StringAPI.hxx"
#include "util/Error.hxx"
#include "system/Error.hxx"
#include "Log.hxx"
#include "Instance.hxx"
#include "Partition.hxx"
#include "dms/util/IOReader.hxx"
#include "dms/util/IOWriter.hxx"
#include "dms/util/IOUtil.hxx"

#include "Main.hxx"

#include <uuid.h>

#include <thread>

namespace Dms {

static constexpr Domain domain("Context");

static const char *context_tbl[] = {
	"mute",
	"usertips",
	"appusertips",
	"pcshare",
	"mediaserver",
	"main_in",
	"paout",
	"preout",
	"powertrigger",
	"alias_name",

	"dmsversion",
	"version",
	"sn",
	"show_mqa_message_when_enable_eq",
};

static const char *dsd_mode_tbl[] = {
	"none",
	"dop",
};

Context::Context(Instance &_instance)
	: db("misc", "dms"), partition(*_instance.partition)
	, usbmonitor(*_instance.event_loop)
{
}

const char *
Context::c_str(unsigned t)
{
	assert(t < ARRAY_SIZE(context_tbl));

	return context_tbl[t];
}

static std::string
get_product_version()
{
	IOReader reader("/system/build.prop");
	const char *line;

	while ((line = reader.readline()) != nullptr) {
		DivideString ds(line, '=');
		if (ds.IsDefined()
			&& StringIsEqual(ds.GetFirst(), "ro.build.display.id")) {
			assert(ds.GetSecond() != nullptr);
			return std::string(ds.GetSecond());
		}
	}

	return std::string();
}

static std::string
generate_uuid()
{
	uuid_rc_t rc;
	uuid_t *uuid;
	unsigned int version = UUID_MAKE_V1;

	if ((rc = uuid_create(&uuid)) != UUID_RC_OK) {
		return std::string();
	}

	if ((rc = uuid_import(uuid, UUID_FMT_STR, "uuid", strlen("uuid"))) != UUID_RC_OK) {
		//return std::string();
	}

	if ((rc = uuid_make(uuid, version)) != UUID_RC_OK) {
		return std::string();
	}

	void *vp = nullptr;
	size_t n;
	if ((rc = uuid_export(uuid, UUID_FMT_STR, &vp, &n)) != UUID_RC_OK) {
		return std::string();
	}
	std::string str = std::string((char*)vp);
	free(vp);

	return str;
}

void
Context::init()
{
	bool ret = product.init();
	if (!ret) {
		FormatDefault(domain, "couldn't find product config, use default config?");
	}
	FormatDefault(domain, "product: %s", product.c_str());
	struct sockaddr mac;
	if (!Dms::get_mac_addr(mac, "eth0")) {
		if (!Dms::get_mac_addr(mac, "wlan0")) {
			// some error
		}
	}

	uuid = db.load("uuid", std::string());
	if (uuid.empty()) {
		uuid = generate_uuid();
		if (uuid.empty()) {
			const char *hex2char = "0123456789abcdef";
			uuid = "00000000-0000-0000-0000-";
			for (unsigned i=0;i<6;i++) {
				uuid.append(1, hex2char[mac.sa_data[i] >> 4]);
				uuid.append(1, hex2char[mac.sa_data[i] & 0x0f]);
			}
		}
		db.store("uuid", uuid);
	}
	FormatDefault(domain, "uuid: %s", uuid.c_str());

	acquireNoException(SN);
	acquireNoException(ALIAS_NAME);

	productversion = get_product_version();
	mute = db.load(c_str(MUTE), false);
	playmode.load();

#ifdef ENABLE_SHAIRPLAY
	auto airplay_init_callback = [&]() {
		system2("rm -rf /data/run/avahi-daemon");
		system2("busybox killall -9 avahi-daemon");
		system2("setprop ctl.start avahi-daemon");
		unsigned seconds = 5;
		while (seconds--) { // wait avahi-daemon start
			sleep(1);
		}
		airplay.Init(IgnoreError());
	};
	std::thread(airplay_init_callback).detach();
#endif
}

void
Context::load()
{
	load(SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ);
	std::string dsd_mode_str = db.load("dsd_mode", "");
	for (unsigned i=0;i<ARRAY_SIZE(dsd_mode_tbl);i++) {
		if (dsd_mode_str == dsd_mode_tbl[i]) {
			dsd_mode = (DsdMode)i;
			break;
		}
	}
	max_dsd_rate = db.load("max_dsd_rate", max_dsd_rate);
}

void
Context::load(unsigned t)
{
	switch (t) {
	case MUTE:
		mute = db.load(c_str(MUTE), false);
		break;
	case SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ:
		show_mqa_message_when_enable_eq = db.load(c_str(SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ), true);
		break;
	default:
		gcc_unreachable();
		assert(false);
	}
}

void
Context::store() const
{
	db.store(c_str(MUTE), mute);
	playmode.store();
	store(SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ);
	db.store("dsd_mode", dsd_mode_tbl[(int)dsd_mode]);
	db.store("max_dsd_rate", max_dsd_rate);
}

void
Context::store(unsigned t) const
{
	switch (t) {
	case MUTE:
		db.store(c_str(MUTE), mute);
		break;
	case SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ:
		db.store(c_str(SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ), show_mqa_message_when_enable_eq);
		break;
	default:
		gcc_unreachable();
		assert(false);
	}
}

void
Context::apply() const
{
	playmode.apply(partition);
	apply(MUTE);
}

#define FILE_MUTE			"/sys/dms-m3/mute"
#define FILE_USER_TIPS		"/sys/dms-m3/usertips"
#define FILE_ALIAS_NAME		"/data/mpd/alias_name"
#define FILE_DMS_VERSION	"/sys/dms-m3/version"
#define FILE_PRODUCT_VERSION	"/system/build.prop"
#define FILE_MAIN_IN		"/sys/dms-m3/main_in"
#define FILE_PA_OUT			"/sys/dms-m3/pa_out"
#define FILE_PRE_OUT		"/sys/dms-m3/sub_out"
#define FILE_POWER_TRIGGER	"/sys/dms-m3/power_trigger"
#define FILE_SN				"/sys/dms-m3/sn"
#define FILE_DAC_PARAMS		"/sys/dms-m3/params"

static const char *context_file[] = {
	"/sys/dms-m3/mute",
	"/sys/dms-m3/usertips",
	"",
	"",
	"",
	"/sys/dms-m3/main_in",
	"/sys/dms-m3/pa_out",
	"/sys/dms-m3/sub_out",
	"/sys/dms-m3/power_trigger",
	"/data/mpd/alias_name",

	"/sys/dms-m3/version",
	"",
	"/sys/dms-m3/sn",
};

void
Context::applyMute(bool on)
{
	IOWriter writer(context_file[MUTE]);
	writer.write(&on, sizeof(on));
}

void
Context::apply(unsigned t) const
{
	switch (t) {
	case MUTE: {
		IOWriter writer(context_file[MUTE]);
		writer.write(&mute, sizeof(mute));
		break;
	}
	default:
		gcc_unreachable();
		assert(false);
	}
}

void
Context::apply(unsigned t, const void *data, unsigned len) const
{
	bool create = false;

	switch (t) {
	case ALIAS_NAME:
		create = true;
	case MUTE:
	case USER_TIPS:
	case MAIN_IN:
	case PA_OUT:
	case PRE_OUT:
	case POWER_TRIGGER: {
		IOWriter writer(context_file[t], create);
		writer.write(data, len);
		break;
	}
	default:
		gcc_unreachable();
		assert(false);
	}
}

void
Context::acquire()
{
	poweron.acquire();
}


void
Context::acquire(unsigned t)
{
	switch (t) {
	case MUTE: {
		IOReader reader(FILE_MUTE);
		mute = reader.read(false);
		break;
	}

	case ALIAS_NAME: {
		IOReader reader(FILE_ALIAS_NAME);
		aliasname = reader.read(aliasname);
		break;
	}

	case SN: {
		IOReader reader(FILE_SN);
		sn = reader.read("");
		break;
	}

	default:
		gcc_unreachable();
		assert(false);
	}
}

void
Context::acquireNoException(unsigned t)
try {
	acquire(t);
} catch (...) {

}

Context &
GetContext()
{
	assert(instance != nullptr);

	return instance->GetContext();
}

}
