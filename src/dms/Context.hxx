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
 
#pragma once

#include "Compiler.h"
#include "dms/Product.hxx"
#include "dms/Database.hxx"
#include "dms/Playmode.hxx"
#include "dms/Poweron.hxx"
#include "dms/UsbMonitor.hxx"

struct Instance;
struct Partition;

namespace Dms {

enum class DsdMode
{
	NONE,
	DOP,
};

struct Context
{
	enum {
		MUTE,
		USER_TIPS,
		APP_USER_TIPS,
		PC_SHARE,
		MEDIA_SERVER,
		MAIN_IN,
		PA_OUT,
		PRE_OUT,
		POWER_TRIGGER,
		ALIAS_NAME,

		DMS_VERSION,
		PRODUCT_VERSION,
		SN,
		SHOW_MQA_MESSAGE_WHEN_ENABLE_EQ,
	};

	DmsDatabase	db;

	Partition	&partition;

	Product		product;

	bool		mute;

	bool show_mqa_message_when_enable_eq;

	DsdMode dsd_mode = DsdMode::DOP;

	int max_dsd_rate = 512;

	std::string	aliasname;

	Playmode	playmode;

	UsbMonitor usbmonitor;

	std::string sn;

	Poweron		poweron;

	std::string uuid;

	std::string productversion;

	Context(Instance &_instance);

	static const char *c_str(unsigned t);

	void init();

	void load();

	void load(unsigned t);

	void store() const;

	void store(unsigned t) const;

	void apply() const;

	void apply(unsigned t) const;

	static void applyMute(bool on);

	void apply(unsigned t, const void *data, unsigned len) const;

	void acquire();

	void acquire(unsigned t);

	void acquireNoException(unsigned t);

};

extern Context &
GetContext();

}
