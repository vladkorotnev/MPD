/*
 * Copyright 2017 Ryan Walklin
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
#include "mixer/MixerInternal.hxx"
#include "output/plugins/SoundIOOutputPlugin.hxx"
#include "Compiler.h"

class SoundIOMixer final : public Mixer {
	/** the base mixer class */
	SoundIOOutput &self;
	
public:
	SoundIOMixer(SoundIOOutput &_output, MixerListener &_listener)
	:Mixer(soundio_mixer_plugin, _listener),
	self(_output) {}
	
	/* virtual methods from class Mixer */
	void Open() override {
	}
	
	void Close() noexcept override {
	}
	
	int GetVolume() override;
	void SetVolume(unsigned volume) override;
};

static Mixer *
soundio_mixer_init(gcc_unused EventLoop &event_loop, AudioOutput &ao,
				MixerListener &listener,
				gcc_unused const ConfigBlock &block)
{
	return new SoundIOMixer((SoundIOOutput &)ao, listener);
}

int
SoundIOMixer::GetVolume()
{
	return soundio_output_get_volume(self);
}

void
SoundIOMixer::SetVolume(unsigned volume)
{
	soundio_output_set_volume(self, volume);
}

const MixerPlugin soundio_mixer_plugin = {
	soundio_mixer_init,
	false,
};

