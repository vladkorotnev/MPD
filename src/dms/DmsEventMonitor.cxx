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
#include "DmsEventMonitor.hxx"
#include "IOThread.hxx"
#include "client/Client.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "Partition.hxx"
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
#include "fs/io/TextFile.hxx"
#include "Log.hxx"
#include "Idle.hxx"
#include "Instance.hxx"
#include "command/DmsCommands.hxx"
#include "db/update/Service.hxx"
#include "util/Manual.hxx"
#include "StateFile.hxx"
#include "dms/DmsStateFile.hxx"
#include "dms/DmsQueueFile.hxx"
#include "system/Clock.hxx"
#include "dms/Context.hxx"

#include <stdio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#ifndef EV_DMS
#define EV_DMS			0x1b

/*
 * DMS events
*/

#define DMS_MASTER		0
#define DMS_OPTICAL		1
#define DMS_COAXIAL1	2
#define DMS_COAXIAL2	3
#define DMS_AESEBU		4
#define DMS_BLUETOOTH	5
#define DMS_XMOS		6
#define DMS_MAX			0x1f
#define DMS_CNT			(DMS_MAX+1)

#endif

#define DMS_POWERON				0x10
#define DMS_STOP_PLAY			0x11
#define DMS_BLUETOOTH_STATUS	0x12
#define DMS_EVENT		0x15
#define DMS_INPUT_NAME	"dms-m3"
#define MAX_EVENTS 10

#define EVT_BT_MEDATA	0x0001

static const char *device_path = "/dev/input";

static constexpr Domain dms_event_monitor_domain("dms_event_monitor");

extern Instance *instance;
extern StateFile *state_file;
extern DmsStateFile *dms_state_file;
extern DmsQueueFile *dms_queue_file;

static Manual<DmsEventMonitor> dms_event_monitor;

static int open_device(const char *device)
{
    int fd;
    char name[80];

	fd = open(device, O_RDWR);
	if (fd < 0) {
		FormatError(dms_event_monitor_domain, "open %s fail", device);
		return -1;
	}
    
    name[sizeof(name) - 1] = '\0';
    if(ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1) {
        //fprintf(stderr, "could not get device name for %s, %s\n", device, strerror(errno));
        name[0] = '\0';
    }

	FormatDefault(dms_event_monitor_domain, "open %s ", name);
	if (memcmp(name, DMS_INPUT_NAME, sizeof(DMS_INPUT_NAME) != 0)) {
		close(fd);
		return -1;
	}
	return fd;
}

static int scan_dir(const char *dirname)
{
	int fd = -1;
    char devname[PATH_MAX];
    char *filename;
    DIR *dir;
    struct dirent *de;
    dir = opendir(dirname);
    if(dir == NULL)
        return -1;
    strcpy(devname, dirname);
    filename = devname + strlen(devname);
    *filename++ = '/';
    while((de = readdir(dir))) {
        if(de->d_name[0] == '.' &&
           (de->d_name[1] == '\0' ||
            (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        strcpy(filename, de->d_name);
		fd = open_device(devname);
        if (fd>0) {
			break;
		}
    }
    closedir(dir);
	FormatDefault(dms_event_monitor_domain, "read dir %s end", dirname);
    return fd;
}

DmsEventMonitor::DmsEventMonitor(EventLoop &_loop)
	: SocketMonitor(_loop)
//	,DeferredMonitor(_loop)
{
	Open();
	ScheduleRead();
}

DmsEventMonitor::~DmsEventMonitor()
{
	Close();
}

bool DmsEventMonitor::Open()
{
	fdd = scan_dir(device_path);
	FormatDefault(dms_event_monitor_domain, "fdd = %d", fdd);
	if (!fdd) {
		FormatError(dms_event_monitor_domain, "can't find dms input");
		return false;
	}

	SocketMonitor::Open(fdd);

	return true;
}

bool
DmsEventMonitor::OnSocketReady(unsigned)
{
	//FormatDefault(dms_event_monitor_domain, "%s %d", __func__, __LINE__);
	struct input_event event;

	int len = read(fdd, &event, sizeof(event));
	if (len < (int)sizeof(event)) {
		FormatError(dms_event_monitor_domain, "read size error len=%d", len);
	}
	
	FormatDefault(dms_event_monitor_domain, "%04x %04x %08x", event.type, event.code, event.value);
	if (event.type == EV_DMS) {
		mutex.lock();
		m_event = event;
		//DeferredMonitor::Schedule();
		RunDeferred();
		mutex.unlock();
	}

	return true;
}

void DmsEventMonitor::RunDeferred()
{
	struct input_event event = m_event;
	DmsConfig	&df = instance->partition->df;
	DmsControl	&dc = instance->partition->dc;
	Dms::Context &context = Dms::GetContext();
	
	if (event.type == EV_DMS) {
		if (event.code != df.source.toChannel()) {
			df.source.parse((channel_t)event.code);
			idle_add(IDLE_DMS_SOURCE);
		}
		union dms_data data;
		data.data = (unsigned char)event.value;
		if (event.code == CHANNEL_MASTER) {
			// master rate update byself
			/*DmsSRC src;
			if (src.parse((enum DMS_RATE)data.arg.src)
				&& src != df.src) {
				FormatDefault(dms_event_monitor_domain, "SRC (%s)->(%s)", 
					df.src.toString().c_str(), src.toString().c_str());
				df.src = src;
				idle_add(IDLE_DMS_SRC);
			}*/
		} else if (event.code <= CHANNEL_MAX) {
			DmsRate rate;
			DmsSRC src;
			if (rate.parse((enum DMS_RATE)data.arg.rate)
				&& rate != df.rate) {
				FormatDefault(dms_event_monitor_domain, "rate (%s)->(%s)", 
					df.rate.toString().c_str(), rate.toString().c_str());
				df.bluetooth.status.elapsed_time = SongTime::zero();
				df.bluetooth.start_time_point = MonotonicClockMS();
				if (df.rate.value == RATE_NONE &&
					rate.value != RATE_NONE) {
					df.bluetooth.status.state = PlayerState::PLAY;
					if (df.bluetooth.codec == DmsBluetooth::CODEC_NONE) {
						dc.readBluetoothStatus(df.bluetooth);
						DmsControl::readMetadata(df.bluetooth.tag, df.bluetooth.codecString());
					}
				} else if (df.rate.value != RATE_NONE &&
					rate.value == RATE_NONE) {
					df.bluetooth.status.state = PlayerState::STOP;
				}
				df.rate = rate;
				idle_add(IDLE_DMS_RATE);
			}
			if (src.parse((enum DMS_RATE)data.arg.src)
				&& src != df.src) {
				FormatDefault(dms_event_monitor_domain, "SRC (%s)->(%s)", 
					df.src.toString().c_str(), src.toString().c_str());
				df.src = src;
				idle_add(IDLE_DMS_SRC);
			}
		} else if (event.code == DMS_POWERON) {
			context.poweron.parse((unsigned char)event.value);
			if (context.poweron.isRunning()) {
				/*DmsStartupTask *startup_task = GetDmsStartupTask();
				if (startup_task != nullptr) {
					startup_task->Start();
				}*/
			} else {
				/*DmsStartupTask *startup_task = GetDmsStartupTask();
				if (startup_task != nullptr) {
					startup_task->Stop();
				}*/
			}
			if (context.poweron.isOff() ||
				context.poweron.isOffing()) {
				DmsBrightness bri;
				if (dc.readBrightness(bri) && bri.value != 0) {
					FormatDefault(dms_event_monitor_domain, "brightness (%s)->(%s)", 
						df.brightness.toString().c_str(), bri.toString().c_str());
					df.brightness = bri;
				}
				instance->partition->Stop();
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
				if (df.source.isMaster() && !df.source.isSourceScreen()) {
					std::string path = df.source.getQueuePath();
					dc.saveQueue(path);
				}
#endif
				dc.clearQueuePath();
				instance->partition->ClearQueue();
				unmountAll();
				if (context.poweron.isOff()) {
					df.source.reset();
				}
				
#ifdef CLEAN_ALL_QUEUE_WHEN_POWEROFF
				DmsControl::deleteAllQueueFile();
#endif
			} else if (context.poweron.isOn()) {
			}
			idle_add(IDLE_POWERON);
		} else if (event.code == DMS_STOP_PLAY) {
			instance->partition->Stop();
			if (instance->update != nullptr) {
				instance->update->CancelAllAsync();
			}
			idle_add(IDLE_LOCAL);
		} else if (event.code == DMS_BLUETOOTH_STATUS) {
			DmsBluetooth bluetooth;
			FormatDefault(dms_event_monitor_domain, "sta %d", 
				data.bsta.sta);
			FormatDefault(dms_event_monitor_domain, "codec %d", 
				data.bsta.codec);
			bluetooth.parse(DmsBluetooth::STATE(data.bsta.sta));
			bluetooth.parse(DmsBluetooth::CODEC(data.bsta.codec));
			if (bluetooth.codec != DmsBluetooth::CODEC_NONE) {
				if (!df.bluetooth.tag.HasType(TAG_SUFFIX)) {
					TagBuilder tb(df.bluetooth.tag);
					tb.AddItem(TAG_SUFFIX, bluetooth.codecString().c_str());
					tb.Commit(df.bluetooth.tag);
				}
			}
			FormatDefault(dms_event_monitor_domain, "b_status (%s)->(%s)", 
				df.bluetooth.stateString().c_str(), bluetooth.stateString().c_str());
			FormatDefault(dms_event_monitor_domain, "b_codec (%s)->(%s)", 
				df.bluetooth.codecString().c_str(), bluetooth.codecString().c_str());
			df.bluetooth.state = bluetooth.state;
			df.bluetooth.codec = bluetooth.codec;
			idle_add(IDLE_DMS_BLUETOOTH);
		} else if (event.code == DMS_EVENT) {
			if (event.value & EVT_BT_MEDATA) {
				dc.readBluetoothStatus(df.bluetooth);
				DmsControl::readMetadata(df.bluetooth.tag, df.bluetooth.codecString());
				df.bluetooth.status.elapsed_time = SongTime::zero();
				df.bluetooth.start_time_point = MonotonicClockMS();
				df.bluetooth.status.total_time = df.bluetooth.tag.duration;
				df.bluetooth.id++;
				idle_add(IDLE_PLAYLIST);
			}
		} else {
			FormatDefault(dms_event_monitor_domain, "unknown cmd/value:0x%x 0x%x", event.code, event.value); 
		}
			
	}

}

void DmsEventMonitorInitialize(EventLoop &loop)
{
	dms_event_monitor.Construct(loop);
}

void DmsEventMonitorDeinitialize()
{
	dms_event_monitor.Destruct();
}

