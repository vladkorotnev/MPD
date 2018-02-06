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
#include "event/SocketMonitor.hxx"
#include "event/TimeoutMonitor.hxx"
#include "dms/Usb.hxx"
#include "thread/Mutex.hxx"

#include <mutex>

class EventLoop;

namespace Dms {

class UsbMonitor: public SocketMonitor, TimeoutMonitor
{
	mutable Mutex mutex;
	UsbStorage usbs;

public:
	UsbMonitor(EventLoop &loop);

	~UsbMonitor();

	void Start();

	void Stop();

	UsbStorage LockGet() {
		std::lock_guard<Mutex> locker(mutex);
		TimeoutMonitor::Schedule(100);
		return usbs;
	}

	UsbStorage LockUpdate();

protected:
	/**
	 * @return false if the socket has been closed
	 */
	virtual bool OnSocketReady(unsigned flags) override;

	virtual void OnTimeout() override;
};

}
