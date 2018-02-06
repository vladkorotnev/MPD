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
 
#ifndef DMS_CONTROL_HXX
#define DMS_CONTROL_HXX

#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"
#include "thread/Thread.hxx"
#include "client/Client.hxx"
#include "DmsArgs.hxx"
#include <string.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

class DmsQueueFile;

#define CLEAN_ALL_QUEUE_WHEN_POWEROFF

struct dms_arg {
	unsigned char		src		: 4;
	unsigned char		rate	: 4;
};
struct dms_blue_sta {
	unsigned char		codec	: 4;
	unsigned char		sta		: 4;
};


union dms_data {
	struct dms_arg	arg;
	unsigned char	volume;
	unsigned char	enable;
	unsigned char	version;
	unsigned char	data;
	struct dms_blue_sta bsta;
};

struct dms_struct{
	unsigned char		cmd;
	union dms_data		data;
	unsigned char		crc;
};
typedef struct dms_struct dms_t;

bool writeFile(std::string filename_utf8, const void *data, int length, bool creat);

bool writeFile(std::string filename_utf8, const void *data, int length);

bool readFile(std::string filename_utf8, void *data, int length);

size_t readAll(std::string filename_utf8, void *data, int length);

int system2(const char *cmdstring);

int system_with_back(const char* cmd, std::string &buf);

bool get_mac_addr(struct sockaddr &addr, const char *interface);

bool get_active_mac_addr(struct sockaddr &addr, const char *interface = "eth0");

DmsSRC amendDSDSRC(const DmsSRC &_src, const AudioFormat &in);

class DmsControl
{
public:
	DmsControl() {}

	~DmsControl() {}

	bool readSource(DmsSource &source);

	bool readSource(DmsSource &source, DmsRate &rate, DmsSRC &src);

	bool writeSource(enum DMS_SOURCE source, enum DMS_RATE rate = RATE_NONE);

	bool readVolume(DmsVolume &volume);

	bool writeVolume(unsigned char volume);

	bool readTube(DmsBool &on);

	bool writeTube(bool on);

	bool readVersion(DmsVersion &version);

	bool readSRC(DmsSRC &src);

	bool writeSRC(enum DMS_SOURCE source, enum DMS_RATE src);

	bool readSRCCache(DmsSRC &src);

	bool writeSRCCache(const DmsSRC &src);

	bool readRate(DmsRate &rate);

	bool readIr(DmsIr &ir);

	bool writeIr(enum DMS_IR ir);

	bool readBrightness(DmsBrightness &brightness);

	bool writeBrightness(unsigned char brightness);

	bool readBacklightPower(bool &on);

	bool writeBacklightPower(bool on);

	bool readUsb(DmsUsb &usb);

	bool readUsbs(std::list<DmsUsb> &usbs);

	//bool readMute(DmsBool&on);

	//bool writeMute(bool on);

	static bool readPopMute(DmsBool &on);

	static bool writePopMute(bool on);

	static bool writeHMute(bool on);

	static bool writeSongEnd(bool on);

	static bool writeSeekDsdPop(bool on);

	static inline void setEndFlag(bool on) {flag_end = on;}

	static inline bool getEndFlag() {return flag_end;}
	
	bool readUpdate(DmsUpdate &update);

	bool writeUpdate(const char *s, int length);

	void setDmsQueueFile(DmsQueueFile	*s);

	void loadQueue(std::string str);

	void clearQueuePath();
	
	void saveQueue(std::string str);
	
	void saveQueue();

	void stopQueueSchedule();
	
	bool readPoweron(DmsPoweron &poweron);

	bool writePoweron(unsigned char on);

	bool readBluetoothStatus(DmsBluetooth &bluetooth);

	static bool readSn(std::string &str);
	
	bool writeUserTips(bool on);

	static bool writeDSD512(bool on);

	static bool readUsbId(std::string &id, enum DMS_SOURCE source, std::string path);

	static void deleteAllQueueFile();
	
	static bool readMpdRunCnt(unsigned long &cnt);

	static bool writeMpdRunCnt(unsigned long cnt);

	bool readBypass(DmsBool &on);

	bool writeBypass(bool on);

	static bool readMetadata(Tag &tag, const std::string &codec);

private:
	static bool flag_end;

	DmsQueueFile	*dmsQueueFile;
};


#endif
