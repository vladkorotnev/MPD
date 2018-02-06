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
#include "DmsControl.hxx"
#include "client/Client.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "Partition.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Domain.hxx"
#include "util/StringUtil.hxx"
#include "util/NumberParser.hxx"
#include "fs/AllocatedPath.hxx"
#include "fs/Traits.hxx"
#include "fs/FileSystem.hxx"
#include "fs/NarrowPath.hxx"
#include "fs/io/FileOutputStream.hxx"
#include "fs/io/BufferedOutputStream.hxx"
#include "fs/io/FileReader.hxx"
#include "Log.hxx"
#include "fs/io/TextFile.hxx"
#include "dms/DmsQueueFile.hxx"
#include "tag/TagBuilder.hxx"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include <ifaddrs.h>

static constexpr Domain dms_control_domain("dms_control");

#define PATH_MPD_QUEUE	"/data/mpd/queue"

#define PATH_DMS_SOURCE		"/sys/dms-m3/source"
#define PATH_DMS_EVENT		"/sys/dms-m3/event"
#define PATH_DMS_SRC		"/sys/dms-m3/source"
#define PATH_DMS_SRC_CACHE	"/sys/dms-m3/src_cache"
#define PATH_DMS_VOLUME		"/sys/dms-m3/volume"
#define PATH_DMS_MUTE		"/sys/dms-m3/mute"
#define PATH_DMS_POP_MUTE	"/sys/dms-m3/pop_mute"
#define PATH_DMS_SONG_END	"/sys/dms-m3/song_end"
#define PATH_DMS_SEEK_DSD_POP	"/sys/dms-m3/seek_dsd_pop"
#define PATH_DMS_HMUTE		"/sys/dms-m3/hmute"
#define PATH_DMS_TUBE		"/sys/dms-m3/tube"
#define PATH_DMS_VERSION	"/sys/dms-m3/version"
#define PATH_BRIGHTNESS		"/sys/dms-m3/brightness"
#define PATH_BL_POWER		"/sys/dms-m3/bl_power"
#define PATH_IR_FRONT		"/sys/devices/platform/ir-rc5.0/enable"
#define PATH_IR_REAR		"/sys/devices/platform/ir-rc5.1/enable"
#define PATH_DMS_UPDATE		"/sys/dms-m3/update"
#define PATH_DMS_POWERON	"/sys/dms-m3/poweron"
#define PATH_DMS_BLUETOOTH_STATUS	"/sys/dms-m3/bluetooth_status"
#define PATH_DMS_SN			"/sys/dms-m3/sn"
#define PATH_USERTIPS		"/sys/dms-m3/usertips"
#define PATH_DSD512			"/sys/dms-m3/dsd512"
#define PATH_MPD_RUN_CNT	"/sys/dms-m3/mpd_run_cnt"
#define PATH_DMS_BYPASS		"/sys/dms-m3/bypass"
#define PATH_DMS_METADATA	"/sys/dms-m3/metadata"

int system2(const char *cmdstring)
{
	pid_t pid;
	int status;

	if (cmdstring == nullptr) {
		return 1;
	}

	if ((pid = fork()) < 0) {
		status = -1;
	} else if (pid == 0) {
		execl("/system/bin/sh", "sh", "-c", cmdstring, nullptr);
		exit(127);
	} else {
		while(waitpid(pid, &status, 0) < 0) {
			if(errno != EINTR) {
				status = -1;
				break;
			}
		}
	}

	return status;
}

int system_with_back(const char* cmd, std::string &buf)
{
  int fd[2];
  int bak_fd;
  int new_fd;

  if(pipe(fd))   {
    printf("pipe error!\n");
    return -1;
  }

  bak_fd = dup(STDOUT_FILENO);
  new_fd = dup2(fd[1], STDOUT_FILENO);

  system2(cmd);
  char buffer[4096];
  size_t len = read(fd[0], buffer, sizeof(buffer)-1);
  if (len > 0) {
	  buf.append(buffer, len);
  }
  dup2(bak_fd, new_fd);
  close(fd[0]);
  close(fd[1]);
  close(bak_fd);

  return len;
}

bool get_mac_addr(struct sockaddr &addr, const char *interface)
{
	struct ifreq ifreq;
	int sock;

	if (interface == nullptr) {
		return false;
	}
	sock = socket(AF_INET,SOCK_STREAM, 0);
	if (sock < 0) {
		return false;
	}
	strcpy(ifreq.ifr_name, interface);
	if(ioctl(sock, SIOCGIFHWADDR, &ifreq) < 0) {
		return false;
	}
	memcpy(&addr, &ifreq.ifr_hwaddr, sizeof(addr));

	return true;
}

bool get_active_mac_addr(struct sockaddr &addr, const char *interface)
{
	struct ifaddrs *ifa = NULL, *ifList;
	bool ret;

	if (getifaddrs(&ifList) < 0) {
		return false;
	}
	for (ifa = ifList; ifa != NULL; ifa = ifa->ifa_next) {
		if(ifa->ifa_addr->sa_family == AF_INET) {
			if((ifa->ifa_flags & IFF_UP)
				&& (ifa->ifa_flags & IFF_BROADCAST)
				&& (ifa->ifa_flags & IFF_MULTICAST)
				&& !(ifa->ifa_flags & IFF_LOOPBACK)) {
				interface = ifa->ifa_name;
				break;
			}
		}
	}

	ret = get_mac_addr(addr, interface);
	freeifaddrs(ifList);
	return ret;
}

DmsSRC amendDSDSRC(const DmsSRC &_src, const AudioFormat &in)
{
	DmsSRC src;
	enum DMS_RATE dsd_rate;

	switch (in.format) {
	case SampleFormat::DSD:
		if (in.sample_rate >= 2822400) {
			dsd_rate = RATE_DSD512;
		} else if (in.sample_rate >= 2822400/2) {
			dsd_rate = RATE_DSD256;
		} else if (in.sample_rate >= 2822400/4) {
			dsd_rate = RATE_DSD128;
		} else if (in.sample_rate >= 2822400/8) {
			dsd_rate = RATE_DSD64;
		} else {
			dsd_rate = RATE_DSD64;
		}
		if (_src.value >= RATE_DSD64 && _src.value <= RATE_DSD512) {
			if (_src.value > dsd_rate) {
				dsd_rate = _src.value;
			}
		}
		src.value = dsd_rate;
		break;
	default:
		if (!in.IsDefined() || _src.value == RATE_BYPASS) {
			src.value = _src.value;
		} else {
			enum DMS_RATE pcm_rate;
			if (in.sample_rate >= 768000) {
				pcm_rate = RATE_768K;
			} else if (in.sample_rate >= 705000) {
				pcm_rate = RATE_705K6;
			} else if (in.sample_rate >= 384000) {
				pcm_rate = RATE_384K;
			} else if (in.sample_rate >= 352000) {
				pcm_rate = RATE_352K8;
			} else if (in.sample_rate >= 192000) {
				pcm_rate = RATE_192K;
			} else if (in.sample_rate >= 176000) {
				pcm_rate = RATE_176K4;
			} else if (in.sample_rate >= 96000) {
				pcm_rate = RATE_96K;
			} else if (in.sample_rate >= 88200) {
				pcm_rate = RATE_88K2;
			} else if (in.sample_rate >= 48000) {
				pcm_rate = RATE_48K;
			} else if (in.sample_rate >= 44100) {
				pcm_rate = RATE_44K1;
			} else {
				pcm_rate = RATE_44K1;
			}
			src.value = pcm_rate > _src.value ? pcm_rate : _src.value;
			switch (pcm_rate) {
			case RATE_44K1:
			case RATE_88K2:
			case RATE_176K4:
			case RATE_352K8:
			case RATE_705K6:
				if (src.value == RATE_384K) {
					src.value = RATE_352K8;
				} else if (src.value == RATE_768K) {
					src.value = RATE_705K6;
				}
				break;
			case RATE_48K:
			case RATE_96K:
			case RATE_192K:
			case RATE_384K:
			case RATE_768K:
				if (src.value == RATE_352K8) {
					src.value = RATE_384K;
				} else if (src.value == RATE_705K6) {
					src.value = RATE_768K;
				}
				break;
			default:
				break;
			}
		}
		break;
	}

	return src;
}

bool writeFile(std::string filename_utf8, const void *data, int length, bool creat)
{
	int fd;
	if (creat) {
		fd = open(filename_utf8.c_str(), O_RDWR | O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);
	} else {
		fd = open(filename_utf8.c_str(), O_WRONLY);
	}
	if (fd < 0) {
		FormatDefault(dms_control_domain, "%s %d open file(%s) failed",
			__func__, __LINE__, filename_utf8.c_str());
		return false;
	}
	int len = write(fd, data, length);
	if (len <= 0) {
		FormatDefault(dms_control_domain, "%s %d filename=%s write failed len(%d) != length(%d)",
			__func__, __LINE__, filename_utf8.c_str(), len, length);
		close(fd);
		return false;
	}
	close(fd);

	return true;
}

bool writeFile(std::string filename_utf8, const void *data, int length)
{
	return writeFile(filename_utf8, data, length, false);
}

bool readFile(std::string filename_utf8, void *data, int length)
{
	const auto filename_fs =
		AllocatedPath::FromUTF8(filename_utf8.c_str());
	if (filename_fs.IsNull()) {
		FormatError(dms_control_domain, "%s %d filename=%s",
			__func__, __LINE__, filename_utf8.c_str());
		return false;
	}

	FileReader fr(filename_fs, IgnoreError());
	if (!fr.IsDefined()) {
		FormatError(dms_control_domain, "%s %d filename=%s",
			__func__, __LINE__, filename_utf8.c_str());
		return false;
	}

	int n = fr.Read(data, length, IgnoreError());
	fr.Close();
	if (n) {
		return true;
	}
	FormatError(dms_control_domain, "%s %d filename=%s n(%d)!=length(%d)",
		__func__, __LINE__, filename_utf8.c_str(), n, length);

	return false;
}

size_t readAll(std::string filename_utf8, void *data, int length)
{
	const auto filename_fs =
		AllocatedPath::FromUTF8(filename_utf8.c_str());
	if (filename_fs.IsNull()) {
		FormatError(dms_control_domain, "%s %d filename=%s",
			__func__, __LINE__, filename_utf8.c_str());
		return -1;
	}

	FileReader fr(filename_fs, IgnoreError());
	if (!fr.IsDefined()) {
		FormatError(dms_control_domain, "%s %d filename=%s",
			__func__, __LINE__, filename_utf8.c_str());
		return -1;
	}

	size_t n = fr.Read(data, length, IgnoreError());
	fr.Close();

	return n;
}

bool DmsControl::readSource(DmsSource &source)
{
	dms_t dms;
	
	return (readFile(PATH_DMS_SOURCE, (void*)&dms, sizeof(dms))
		&& source.parse((channel_t)dms.cmd));
}

bool DmsControl::readSource(DmsSource &source, DmsRate &rate, DmsSRC &src)
{
	dms_t dms;
	
	return (readFile(PATH_DMS_SOURCE, (void*)&dms, sizeof(dms))
		&& source.parse((channel_t)dms.cmd)
		&& rate.parse((enum DMS_RATE)dms.data.arg.rate)
		&& src.parse((enum DMS_RATE)dms.data.arg.src));
}

bool DmsControl::writeSource(enum DMS_SOURCE source, enum DMS_RATE src)
{
	dms_t dms;

	dms.cmd = source_to_channel(source);
	dms.data.arg.rate = RATE_NONE;
	dms.data.arg.src = src;

	return writeFile(PATH_DMS_SOURCE, (void*)&dms, sizeof(dms));
}

bool DmsControl::readVolume(DmsVolume &volume)
{
	unsigned char vol;
	
	return (readFile(PATH_DMS_VOLUME, (void*)&vol, sizeof(vol))
		&& volume.parse(vol));
}

bool DmsControl::writeVolume(unsigned char volume)
{
	return writeFile(PATH_DMS_VOLUME, (void*)&volume, sizeof(volume));
}

bool DmsControl::readTube(DmsBool &tube)
{
	bool on;
	return (readFile(PATH_DMS_TUBE, (void*)&on, sizeof(on))
		&& tube.parse(on));
}

bool DmsControl::writeTube(bool on)
{
	return writeFile(PATH_DMS_TUBE, (void*)&on, sizeof(on));
}

bool DmsControl::readVersion(DmsVersion &version)
{
	char v[16] = {0};
	
	return (readFile(PATH_DMS_VERSION, (void*)v, 15)
		&& version.parse(v));
}

bool DmsControl::readSRC(DmsSRC &src)
{
	dms_t dms;
	
	return (readFile(PATH_DMS_SOURCE, (void*)&dms, sizeof(dms))
		&& src.parse((enum DMS_RATE)dms.data.arg.src));
}

bool DmsControl::writeSRC(enum DMS_SOURCE source, enum DMS_RATE src)
{
	return writeSource(source, src);
}

bool DmsControl::readSRCCache(DmsSRC &src)
{
	char s[10];
	
	return (readFile(PATH_DMS_SRC_CACHE, (void*)s, sizeof(s))
		&& src.parse(s));
}

bool DmsControl::writeSRCCache(const DmsSRC &src)
{
	std::string s = src.toString();
	if (s.empty()) {
		return false;
	}
	FormatDefault(dms_control_domain, "write SRC(%s)", s.c_str());

	return writeFile(PATH_DMS_SRC_CACHE, s.c_str(), s.size());
}

bool DmsControl::readRate(DmsRate &rate)
{
	dms_t dms;
	
	return (readFile(PATH_DMS_SOURCE, (void*)&dms, sizeof(dms))
		&& rate.parse((enum DMS_RATE)dms.data.arg.rate));
}

bool DmsControl::readIr(DmsIr &ir)
{
	char temp1, temp2;
	bool ret1 = readFile(PATH_IR_FRONT, (void*)(&temp1), 1);
	bool ret2 = readFile(PATH_IR_REAR, (void*)(&temp2), 1);
	bool front = ret1 && temp1 == '1';
	bool rear = ret2 && temp2 == '1';

	if (!ret1 && !ret2)
		return false;
	return ir.parse(front, rear);
}

bool DmsControl::writeIr(enum DMS_IR ir)
{
	return (writeFile(PATH_IR_FRONT, ((ir == IR_FRONT) || (ir == IR_BOTH)) ? "1" : "0", 1)
		&& writeFile(PATH_IR_REAR, ((ir == IR_REAR) || (ir == IR_BOTH)) ? "1" : "0", 1));
}

bool DmsControl::readBrightness(DmsBrightness &brightness)
{
	char bri[10];
	char *briptr = bri;

	memset(bri, 0, sizeof(bri));
	if (readFile(PATH_BRIGHTNESS, (void*)(bri), sizeof(bri)-1)) {
		briptr = Strip(briptr);
		return brightness.parse(briptr);
	}
	return false;
}

bool DmsControl::writeBrightness(unsigned char brightness)
{
	char bri[10];
	int n = snprintf(bri, (sizeof(bri)-1), "%u", brightness);
	
	return writeFile(PATH_BRIGHTNESS, (void*)(bri), n);
}

bool DmsControl::readBacklightPower(bool &on)
{
	char enable;

	if (readFile(PATH_BL_POWER, (void*)(&enable), sizeof(enable))) {
		on = enable == '2' ? false : true;
		return true;
	}
	return false;
}

bool DmsControl::writeBacklightPower(bool on)
{
	char enable[10];
	int n = snprintf(enable, (sizeof(enable)-1), "%u", on ? 1 : 2);
	
	return writeFile(PATH_BL_POWER, (void*)(enable), n);
}

bool DmsControl::readUsb(DmsUsb &usb)
{
	Error error;
	const auto filename_fs =
		AllocatedPath::FromUTF8("/proc/mounts");
	if (filename_fs.IsNull()) {
		FormatError(dms_control_domain, "%s %d", __func__, __LINE__);
		return false;
	}
	TextFile file(filename_fs, error);
	if (file.HasFailed()) {
		FormatError(dms_control_domain, "%s %d", __func__, __LINE__);
		return false;
	}
	
	const char *line;
	while ((line = file.ReadLine()) != nullptr) {
		usb.parse(line);
	}
	
	return true;
}

bool DmsControl::readUsbs(std::list<DmsUsb> &usbs)
{
	Error error;
	const auto path_mounts = AllocatedPath::FromUTF8("/proc/mounts");
	if (path_mounts.IsNull()) {
		FormatError(dms_control_domain, "%s %d", __func__, __LINE__);
		return false;
	}
	TextFile file_mounts(path_mounts, error);
	if (file_mounts.HasFailed()) {
		FormatError(dms_control_domain, "%s %d", __func__, __LINE__);
		return false;
	}
	
	char *line;
	while ((line = file_mounts.ReadLine()) != nullptr) {
		if (strcasestr(line, "/mnt/usb_storage/USB_DISK") != nullptr
			|| strcasestr(line, "/mnt/external_sd") != nullptr) {
			char *str_node = strtok(line, " ");
			if (str_node == nullptr) {
				continue;
			}
			char *path = strtok(NULL, " ");
			char *node = strrchr(str_node, '/');
			if (!(node && path)) {
				continue;
			}
			node++;
			std::string node_fs = "/sys/dev/block/";
			node_fs.append(node);

			char buffer[256];
			ssize_t size = readlink(node_fs.c_str(), buffer, 256);
			if (size < 0)
				continue;
			buffer[size] = '\0';
			const char *usbx = strstr(buffer, "-1.");
			if (usbx == nullptr) {
				usbx = strstr(buffer, "mmc");
				if (usbx == nullptr) {
					usbx = "usb2";
				} else {
					usbx = "usb1";
				}
			}
			if (usbx == nullptr) {
				continue;
			}
			enum DMS_SOURCE source;
			switch (usbx[sizeof("-1.")-1]) {
			case '1':
				source = SOURCE_SD;
				break;
			case '2':
				source = SOURCE_USB_1;
				break;
			case '3':
				source = SOURCE_USB_2;
				break;
			case '4':
				source = SOURCE_USB_3;
				break;
			default:
				FormatError(dms_control_domain, "%s %d usbx=%c", __func__, __LINE__,usbx[0]);
				continue;
			}
			std::size_t found2 = 0;
			std::string str_path = path;
			while (1) {
				found2 = str_path.find("\\040", found2);
				if (found2 == std::string::npos) {
					break;
				}
				str_path.replace(found2, 4, 1, ' ');
			};
			std::string id("usb_");
			if (!readUsbId(id, source,str_path)) {
				id = "usb_none_id";
			}
			bool found = false;
			for(auto &usb:usbs) {
				if (usb.value == source) {
					found = true;
					usb.ids.push_back(id);
					usb.nodes.push_back(std::string(node));
					usb.paths.push_back(std::string(str_path));
					break;
				}
			}
			if (found) {
				continue;
			}
			DmsUsb usb;
			usb.value = source;
			usb.is_valid = true;
			usb.ids.push_back(id);
			usb.nodes.push_back(std::string(node));
			usb.paths.push_back(std::string(str_path));
			usbs.push_back(usb);
		}
	}
	
	return true;
}

/*
bool DmsControl::readMute(DmsBool &mute)
{
	bool on;
	return (readFile(PATH_DMS_MUTE, (void*)&on, sizeof(on))
		&& mute.parse(on));
}

bool DmsControl::writeMute(bool on)
{
	return writeFile(PATH_DMS_MUTE, (void*)&on, sizeof(on));
}
*/

bool DmsControl::readPopMute(DmsBool &mute)
{
	bool on;
	return (readFile(PATH_DMS_POP_MUTE, (void*)&on, sizeof(on))
		&& mute.parse(on));
}

bool DmsControl::flag_end = false;
bool DmsControl::writePopMute(bool on)
{
	FormatDefault(dms_control_domain, "%s %d on=%d", __func__, __LINE__, on);
	return writeFile(PATH_DMS_POP_MUTE, (void*)&on, sizeof(on));
}

bool DmsControl::writeHMute(bool on)
{
	FormatDefault(dms_control_domain, "%s %d on=%d", __func__, __LINE__, on);
	return writeFile(PATH_DMS_POP_MUTE, (void*)&on, sizeof(on));
}

bool DmsControl::writeSongEnd(bool on)
{
	FormatDefault(dms_control_domain, "%s %d on=%d", __func__, __LINE__, on);
	return writeFile(PATH_DMS_SONG_END, (void*)&on, sizeof(on));
}

bool DmsControl::writeSeekDsdPop(bool on)
{
	FormatDefault(dms_control_domain, "%s %d on=%d", __func__, __LINE__, on);
	return writeFile(PATH_DMS_SEEK_DSD_POP, (void*)&on, sizeof(on));
}

bool DmsControl::readUpdate(DmsUpdate &update)
{
	char v;

	if (readFile(PATH_DMS_UPDATE, (void*)(&v), 1)) {
		return update.parse(v);
	}
	return false;
}

bool DmsControl::writeUpdate(const char *s, int length)
{	
	return writeFile(PATH_DMS_UPDATE, (const void*)(s), length);
}

void DmsControl::setDmsQueueFile(DmsQueueFile	*s)
{
	dmsQueueFile = s;
}

void DmsControl::loadQueue(std::string str)
{ 
	std::string path = "/data/mpd/queue/";
	path.append(str);

	if (dmsQueueFile) {
		dmsQueueFile->setPath(path);
		dmsQueueFile->Read();
	}
}

void DmsControl::saveQueue(std::string str)
{
	std::string path = "/data/mpd/queue/";
	path.append(str);

	if (dmsQueueFile) {
		dmsQueueFile->setPath(path);
		dmsQueueFile->Write();
	}
}

void DmsControl::clearQueuePath()
{
	std::string path = "";

	if (dmsQueueFile) {
		dmsQueueFile->setPath(path);
	}
}

void DmsControl::saveQueue()
{
	if (dmsQueueFile) {
		dmsQueueFile->Write();
	}
}

void DmsControl::stopQueueSchedule()
{
	if (dmsQueueFile) {
		dmsQueueFile->Stop();
	}
}

bool DmsControl::readPoweron(DmsPoweron &poweron)
{
	unsigned char on;
	return (readFile(PATH_DMS_POWERON, (void*)&on, sizeof(on))
		&& poweron.parse(on));
}

bool DmsControl::writePoweron(unsigned char on)
{
	return writeFile(PATH_DMS_POWERON, (void*)&on, sizeof(on));
}

bool DmsControl::readBluetoothStatus(DmsBluetooth &bluetooth)
{
	struct dms_blue_sta bsta;
	return (readFile(PATH_DMS_BLUETOOTH_STATUS, (void*)&bsta, sizeof(bsta))
		&& bluetooth.parse(DmsBluetooth::STATE(bsta.sta))
		&& bluetooth.parse(DmsBluetooth::CODEC(bsta.codec)));
}

bool DmsControl::readSn(std::string &str)
{
	char buf[50] = {0};
	str.clear();
	bool ret = readFile(PATH_DMS_SN, (void*)buf, sizeof(buf)-1);
	if (ret) {
		str.append(buf);
		return true;
	}
	return false;
}

bool DmsControl::readUsbId(std::string &id, enum DMS_SOURCE source, std::string path)
{
	char usbx;
	switch (source) {
	case SOURCE_SD:
		usbx = '1';
		break;
	case SOURCE_USB_1:
		usbx = '2';
		break;
	case SOURCE_USB_2:
		usbx = '3';
		break;
	case SOURCE_USB_3:
		usbx = '4';
		break;
	default:
		FormatDefault(dms_control_domain, "%s %d source=%d?",
			__func__, __LINE__, source);
		return false;
	}
	char vid[10] = {0};
	char pid[10] = {0};
	char serial[50] = {0};
	int random = 0;
	std::string pathIdProduct("/sys/devices/platform/usb20_host/usb2/2-1/2-1.x/idProduct");
	std::string pathIdVendor("/sys/devices/platform/usb20_host/usb2/2-1/2-1.x/idVendor");
	std::string pathSerialNumber("/sys/devices/platform/usb20_host/usb2/2-1/2-1.x/serial");
	std::string pathStorage = path;
	pathStorage.append("/.dmsmusic");
	int pos = pathIdProduct.find_first_of('x');
	pathIdProduct.replace(pos, 1, 1, usbx);
	pathIdVendor.replace(pos, 1, 1, usbx);
	pathSerialNumber.replace(pos, 1, 1, usbx);
	if (!readFile(pathIdVendor, (void*)vid, sizeof(vid)-1) ||
		!readFile(pathIdProduct, (void*)pid, sizeof(pid)-1)){
		FormatDefault(dms_control_domain, "%s %d read(%s)\n or (%s) fail",
			__func__, __LINE__, pathIdVendor.c_str(), pathIdProduct.c_str());
		return false;
	}
	if (!readFile(pathSerialNumber, (void*)serial, sizeof(serial)-1)) {
		FormatDefault(dms_control_domain, "%s %d read(%s)\n fail",
			__func__, __LINE__, pathSerialNumber.c_str());
	}
	if (!readFile(pathStorage, (void*)(&random), sizeof(random))) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		srand((unsigned int)tv.tv_usec);
		random = rand();
		FormatDefault(dms_control_domain, "%s %d read(%s) fail, random=%d",
			__func__, __LINE__, pathStorage.c_str(), random);
		writeFile(pathStorage, &random, sizeof(random), true);
		if (!readFile(pathStorage, (void*)(&random), sizeof(random))) {
			random = 0;
		}
	}
	vid[4] = 0;
	pid[4] = 0;
	id.append(vid);
	id.append(pid);
	if (random == 0 && strlen(serial)) {
		char *s = Strip(serial);
		id.append(s);
		FormatDefault(dms_control_domain, "%s %d use serial: %s id:%s", __func__, __LINE__, s, id.c_str());
	} else {
		char ra[10];
		sprintf(ra, "%010d", random);
		id.append(ra);
	}
	/*serial[sizeof(serial)-1] = 0;
	if (strlen(serial) > 0) {
		serial[strlen(serial)-1] = 0;
	}
	id.append(serial);*/
	return true;
}

bool DmsControl::writeUserTips(bool on)
{
	char enable = on ? '1' : '0';
	return writeFile(PATH_USERTIPS, (void*)&enable, sizeof(enable));
}

bool DmsControl::writeDSD512(bool on)
{
	char enable = on ? '1' : '0';
	return writeFile(PATH_DSD512, (void*)&enable, sizeof(enable));
}

void DmsControl::deleteAllQueueFile()
{
	struct dirent *ep;
	DIR *dp = opendir(PATH_MPD_QUEUE);
	FormatDefault(dms_control_domain, "deleteAllQueueFile");
	if (dp != nullptr) {
		while ((ep = readdir(dp)) != nullptr) {
			FormatDefault(dms_control_domain, "type: 0x%x name:%s", ep->d_type, ep->d_name);
			if (ep->d_type == DT_REG) {
				std::string filepath = PATH_MPD_QUEUE;
				filepath.append("/");
				filepath.append(ep->d_name);
				if (remove(filepath.c_str()) < 0) {
					FormatError(dms_control_domain, "remove %s fail: %s", ep->d_name, strerror(errno));
				}
			}
		}
		closedir(dp);
	} else {
		FormatError(dms_control_domain, "open %s fail: %s", PATH_MPD_QUEUE, strerror(errno));
	}
}

bool DmsControl::readMpdRunCnt(unsigned long &cnt)
{
	char buf[100];
	
	if (readFile(PATH_MPD_RUN_CNT, (void*)&buf, sizeof(buf))) {
		cnt = ParseUint64(buf);
		return true;
	}
	return false;
}

bool DmsControl::writeMpdRunCnt(unsigned long cnt)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%lu", cnt);
	return writeFile(PATH_MPD_RUN_CNT, (void*)&buf, strlen(buf));
}

bool DmsControl::readBypass(DmsBool &bypass)
{
	bool on;
	return (readFile(PATH_DMS_BYPASS, (void*)&on, sizeof(on))
		&& bypass.parse(on));
}

bool DmsControl::writeBypass(bool on)
{
	return writeFile(PATH_DMS_BYPASS, (void*)&on, sizeof(on));
}

#define TAG_LEN 255
bool DmsControl::readMetadata(Tag &tag, const std::string &codec)
{
	char buf[TAG_LEN * 7];

	memset(buf, 0, sizeof(0));
	int len = readAll(PATH_DMS_METADATA, (void*)buf, sizeof(buf));
	if (len >= 0) {
		TagBuilder tb;
		const char *pdata = buf;
		for (unsigned i=0;i<7;i++) {
			unsigned n = *((const unsigned char*)pdata);
			pdata++;
			if (!StringStartsWith(pdata, "IND:-A") ||
				n < sizeof("IND:-Ax")) { // "IND:-Ax"
				continue;
			}
			pdata += (sizeof("IND:-A") - 1);
			n -= (sizeof("IND:-Ax") - 1); // "IND:-Ax"
			auto dlen = StripRight(pdata+1, n);
			switch (pdata[0]) {
			case '1':
				tb.AddItem(TAG_TITLE, pdata+1, dlen);
				break;
			case '2':
				tb.AddItem(TAG_ARTIST, pdata+1, dlen);
				break;
			case '3':
				tb.AddItem(TAG_ALBUM, pdata+1, dlen);
				break;
			case '4':
				tb.AddItem(TAG_TRACK, pdata+1, dlen);
				break;
			case '5':
				tb.AddItem(TAG_TOTAL_TRACKS, pdata+1, dlen);
				break;
			case '6':
				tb.AddItem(TAG_GENRE, pdata+1, dlen);
				break;
			case '7':
				tb.SetDuration(SignedSongTime::FromMS(ParseInt(pdata+1)));
				break;
			}
			pdata += 1 + n;
		}
		if (!codec.empty()) {
			tb.AddItem(TAG_SUFFIX, codec.c_str());
		}
		tb.Commit(tag);

		return true;
	}

	return false;
}

