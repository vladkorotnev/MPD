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
#include "Usb.hxx"
#include "util/StringUtil.hxx"
#include "dms/util/IOReader.hxx"
#include "dms/util/IOWriter.hxx"
#include "util/StringUtil.hxx"

#include <mntent.h>
#include <syslog.h>
#include <sys/mount.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

namespace Dms {

static void
string_unescape(std::string &str, const char *s, char c)
{
	std::size_t found = 0;
	size_t len = strlen(s);

	while (1) {
		found = str.find(s, found);
		if (found == std::string::npos) {
			break;
		}
		str.replace(found, len, 1, c);
	};
}

static int
get_usbx(std::string fsname)
{
	std::size_t p = fsname.find("vold/");
	if (p != std::string::npos) {
		fsname.erase(p, 5);
	}
	fsname.insert(0, "/sys");

	char buffer[256];
	ssize_t size = readlink(fsname.c_str(), buffer, sizeof(buffer));
	if (size < 0)
		return -1;
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
		return -1;
	}
	return usbx[sizeof("-1.")-1] - 0x30; 
}

static int
get_random()
{
	struct timeval tv;

	gettimeofday(&tv, nullptr);
	srand((unsigned int)tv.tv_usec);

	return rand();
}

static std::string
get_usbid(int x, const std::string &path)
{
	int random = 0;
	std::string pathStorage = path+"/.dmsmusic";
	std::string id;
	try {
		IOReader reader(pathStorage.c_str());
		size_t len = reader.read((void*)(&random), sizeof(random));
		if (len != sizeof(random)) {
			throw -1;
		}
		char ra[11];
		sprintf(ra, "%010d", random);
		id.append(ra);
	} catch (...) {
		try {
			random = get_random();
			IOWriter writer(pathStorage.c_str());
			writer.write((void*)&random, sizeof(random));
			char ra[11];
			sprintf(ra, "%010d", random);
			id.append(ra);
		} catch (...) {
			char vid[10] = {0};
			char pid[10] = {0};
			char serial[50] = {0};
			std::string pathIdProduct("/sys/devices/platform/usb20_host/usb2/2-1/2-1.x/idProduct");
			std::string pathIdVendor("/sys/devices/platform/usb20_host/usb2/2-1/2-1.x/idVendor");
			std::string pathSerialNumber("/sys/devices/platform/usb20_host/usb2/2-1/2-1.x/serial");
			int pos = pathIdProduct.find_first_of('x');
			pathIdProduct.replace(pos, 1, 1, x);
			pathIdVendor.replace(pos, 1, 1, x);
			pathSerialNumber.replace(pos, 1, 1, x);
			try {
				IOReader reader(pathIdVendor.c_str());
				reader.read(vid, sizeof(vid)-1);
				vid[4] = 0;
				id.append(vid);
			} catch (...) {
			}
			try {
				IOReader reader(pathIdProduct.c_str());
				reader.read(pid, sizeof(pid)-1);
				pid[4] = 0;
				id.append(pid);
			} catch (...) {
			}
			try {
				IOReader reader(pathSerialNumber.c_str());
				reader.read(serial, sizeof(serial)-1);
				char *s = Strip(serial);
				id.append(s);
			} catch (...) {
			}
		}
	}

	return id;
}

static std::list<MountUsb>
get_mount_usbs()
{
	std::list<MountUsb> list;

	struct mntent mtpair[2];
	char buf[4096];
	FILE *mountTable = setmntent("/proc/mounts", "r");

	if (!mountTable) {
		return list;
	}

	while (getmntent_r(mountTable, &mtpair[0], buf, sizeof(buf))) {
		if (StringStartsWith(mtpair->mnt_dir, "/mnt/usb_storage/USB_DISK")
			|| StringStartsWith(mtpair->mnt_dir, "/mnt/external_sd")) {
			MountUsb usb;
			usb.fsname = mtpair->mnt_fsname;
			usb.dir = mtpair->mnt_dir;
			string_unescape(usb.dir, "\\040", ' ');
			string_unescape(usb.dir, "\\011", '\t');
			string_unescape(usb.dir, "\\012", '\n');
			usb.usbx = get_usbx(usb.fsname);
			if (usb.usbx > 0) {
				usb.id = get_usbid(usb.usbx, usb.dir);
				list.push_back(usb);
			}
		}
	}
	endmntent(mountTable);

	return list;
}

source_t
MountUsb::toSource() const
{
	switch (usbx) {
	case 1:
		return SOURCE_SD;
	case 2:
		return SOURCE_USB_1;
	case 3:
		return SOURCE_USB_2;
	case 4:
		return SOURCE_USB_3;
	default:
		return SOURCE_NONE;
	}
}

void
UsbStorage::query()
{
	list = get_mount_usbs();
}

static bool
compare_usbx(const MountUsb &first, const MountUsb &second)
{
	return (first.usbx < second.usbx);
}

void
UsbStorage::sort()
{
	list.sort(compare_usbx);
}

static int
source_to_usbx(source_t s)
{
	switch (s) {
	case SOURCE_SD:
		return 1;
	case SOURCE_USB_1:
		return 2;
	case SOURCE_USB_2:
		return 3;
	case SOURCE_USB_3:
		return 4;
	default:
		return -1;
	}
}

bool
UsbStorage::contain(source_t s)
{
	int usbx = source_to_usbx(s);
	assert(usbx != -1);

	for (const auto &i : list) {
		if (i.usbx == usbx) {
			return true;
		}
	}

	return false;
}

MountUsb
UsbStorage::find(source_t s)
{
	int usbx = source_to_usbx(s);
	assert(usbx != -1);

	for (const auto &i : list) {
		if (i.usbx == usbx) {
			return i;
		}
	}

	return MountUsb();
}

}
