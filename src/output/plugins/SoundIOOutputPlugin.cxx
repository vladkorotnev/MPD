/*
 * Copyright 2003-2017 The Music Player Daemon Project
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
#include "SoundIOOutputPlugin.hxx"
#include "../OutputAPI.hxx"
#include "../Wrapper.hxx"
#include "mixer/MixerList.hxx"
#include "util/RuntimeError.hxx"
#include "util/Domain.hxx"
#include "Log.hxx"
#include "pcm/PcmPack.hxx"
#include "util/StringBuffer.hxx"

#include <soundio/soundio.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <chrono>
#include <thread>
#include <string.h>

#define READ_BUFFER_SIZE (1024*1024)

// Methods.
static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max);
static void error_callback(struct SoundIoOutStream *, int err);
static SoundIoFormat convertAudioFormat(SampleFormat format);
static SampleFormat convertSampleFormat(SoundIoFormat format);

class SoundIOOutput {
	friend struct AudioOutputWrapper<SoundIOOutput>;
	AudioOutput base;
	
	struct SoundIo *soundio;
	struct SoundIoDevice *device;
	struct SoundIoOutStream *outstream;
	
	unsigned long ring_buffer_size;
	
	AudioFormat device_format;
	
	SoundIOOutput(const ConfigBlock &block);
	~SoundIOOutput();
	
public:
	static SoundIOOutput *Create(EventLoop &, const ConfigBlock &block) {
		return new SoundIOOutput(block);
	}
	
	void Open(AudioFormat &audio_format);
	void Close() noexcept;
	size_t Play(const void *chunk, size_t size);
	void Cancel();
	bool Pause();
	AudioFormat DeviceFormat();
	double Latency();
	
	int GetVolume() const;
	void SetVolume(unsigned volume);
	
	std::chrono::steady_clock::duration Delay() const noexcept;

	uint8_t* buffer;
	boost::lockfree::spsc_queue<uint8_t> *ring_buffer;
	std::atomic<bool> paused;
	float fade_level;
	std::atomic<float> fade_out_time;
	std::atomic<float> fade_in_time;
};

static constexpr Domain soundio_output_domain("soundio_output");

static void
write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max)
{
	auto *od = (SoundIOOutput *)outstream->userdata;
	const int bytes_per_frame = outstream->bytes_per_frame;
	const int max_bytes = std::min(READ_BUFFER_SIZE, frame_count_max * bytes_per_frame);

	// Initialize buffer to silence
	memset(od->buffer, 0, max_bytes);

	// Try to read as many bytes as we could want to consume.
	int bytes_read = od->ring_buffer->pop(od->buffer, max_bytes);
	int frame_count = std::max(bytes_read / bytes_per_frame, frame_count_min);

#ifdef WIN32
	if (frame_count == 0) {
		// We want to write something, otherwise we'll busy spin.
		frame_count = frame_count_max;
	}
#endif

	// If we don't have anything to do, get out.
	if (frame_count == 0) {
		return;
	}

	// Start the write.
	struct SoundIoChannelArea *areas;
	int err;
	if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
		throw FormatRuntimeError("soundio error: %s", soundio_strerror(err));
	}

	// Write bytes to the output buffer.
	int audio_bytes_written = frame_count * bytes_per_frame;
	uint8_t *output_buffer = (uint8_t *)areas[0].ptr;
	memcpy(output_buffer, od->buffer, audio_bytes_written);

	// Now postprocess the audio in case we're fading in or out.
	bool is_fading = false;
	const bool paused = od->paused;
	int remainder_bytes = 0;

	if (frame_count > 0) {
		// setup fade if required
		float fade_step = 0.0;

		if (paused && od->fade_level > 0.0) {
			// transitioning from play to pause
			fade_step = -1.0f / (outstream->sample_rate * od->fade_out_time);
			is_fading = true;
		} else if (!paused && od->fade_level < 1.0) {
			// transitioning from pause to play
			fade_step = 1.0f / (outstream->sample_rate * od->fade_in_time);
			is_fading = true;
		}

        if (!is_fading && paused) {
            // We're paused, treat them all as reminder bytes and write silence.
            remainder_bytes += audio_bytes_written;
        }
        else if (is_fading) {
			int frames_left = frame_count;
			int16_t sampleS16LE = 0;
			int32_t sampleS24LE = 0;
			float sampleF32LE = 0;
			const int channel_count = outstream->layout.channel_count;

            do {
				frames_left -= 1;
				switch (outstream->format) {
					case SoundIoFormatS16LE:    ///< Signed 16 bit Little Endian
						for (int i = 0; i < channel_count; i++) {
							memcpy(&sampleS16LE, output_buffer, sizeof(int16_t));
							sampleS16LE *= od->fade_level;
							memcpy(output_buffer, &sampleS16LE, sizeof(int16_t));
							output_buffer += sizeof(int16_t);
						}
						break;
					case SoundIoFormatS24LE:    ///< Signed 24 bit Little Endian using low three bytes in 32-bit word
						for (int i = 0; i < channel_count; i++) {
							pcm_unpack_24(&sampleS24LE, output_buffer, output_buffer+1);
							sampleS24LE *= od->fade_level;
							pcm_pack_24(output_buffer, &sampleS24LE, &sampleS24LE+1);
							output_buffer += sizeof(int32_t);
						}
						break;
					case SoundIoFormatFloat32LE: ///< Float 32 bit Little Endian, Range -1.0 to 1.0
						for (int i = 0; i < channel_count; i++) {
							memcpy(&sampleF32LE, output_buffer, sizeof(float));
							sampleF32LE *= od->fade_level;
							memcpy(output_buffer, &sampleF32LE, sizeof(float));
							output_buffer += sizeof(float);
						}
						break;
					default:
						frames_left = 0;
				}
				od->fade_level += fade_step;
				if (paused && od->fade_level <= 0.0) {
					// fade out is done, mute rest of buffer.
					remainder_bytes = frames_left * bytes_per_frame;
					od->fade_level = 0.0;

					// Last, clear out any remaining bytes (not sure if this is needed).
					od->ring_buffer->reset();

					break;
				}
				else if (!paused && od->fade_level >= 1.0) {
					// fade in is done
					od->fade_level = 1.0;
					break;
				}
			} while (frames_left > 0);
		} else {
			output_buffer += audio_bytes_written;
		}
	}

	if (remainder_bytes > 0) {
		// Fill any remainder with silence.
		memset(output_buffer, 0, remainder_bytes);
		if (is_fading) {
			// we're out of audio so stop fading
			od->fade_level = paused ? 0.0 : 1.0;
		}
	}
	
	if ((err = soundio_outstream_end_write(outstream))) {
		throw FormatRuntimeError("soundio error: %s", soundio_strerror(err));
	}
}

/// Optional callback. `err` is always SoundIoErrorStreaming.
/// SoundIoErrorStreaming is an unrecoverable error. The stream is in an
/// invalid state and must be destroyed.
/// If you do not supply error_callback, the default callback will print
/// a message to stderr and then call `abort`.
/// This is called from the SoundIoOutStream::write_callback thread context.
static void
error_callback(struct SoundIoOutStream *, int err)
{
	throw FormatRuntimeError("soundio error: %s", soundio_strerror(err));
}

/// This optional callback happens when the sound device runs out of
/// buffered audio data to play. After this occurs, the outstream waits
/// until the buffer is full to resume playback.
/// This is called from the SoundIoOutStream::write_callback thread context.
static void underflow_callback(struct SoundIoOutStream *) {
	FormatRuntimeError("soundio error: underflow");
}

inline int
SoundIOOutput::GetVolume() const
{
	if (outstream == nullptr)
		return -1;
	return outstream->volume * 100;
}

int
soundio_output_get_volume(SoundIOOutput &soundio)
{
	return soundio.GetVolume();
}

inline void SoundIOOutput::SetVolume(unsigned volume) {
	assert(volume <= 100);
	
	if (outstream == nullptr)
		throw std::runtime_error("soundio output closed");
	
	float level = volume / 100.0;
	
	if (soundio_outstream_set_volume(outstream, level))
		throw std::runtime_error("Unable to set soundio volume");
}

void soundio_output_set_volume(SoundIOOutput &soundio, unsigned volume) {
	soundio.SetVolume(volume);
}

static SoundIoFormat convertAudioFormat(SampleFormat format) {
	switch (format) {
		case SampleFormat::S8:
			return SoundIoFormatS8;        ///< Signed 8 bit
		case SampleFormat::S16:
			return SoundIoFormatS16NE; //< Signed 16 bit Native Endian
		case SampleFormat::S24_P32:
			return SoundIoFormatS24NE; ///< Signed 24 bit Little Endian using low three bytes in 32-bit word
		case SampleFormat::S32:
			return  SoundIoFormatS32NE; ///< Signed 32 bit Native Endian
		case SampleFormat::FLOAT:
			return SoundIoFormatFloat32NE; ///< Float 32 bit Native Endian, Range -1.0 to 1.0
		case SampleFormat::DSD:
			// FIXME: DSD
		default:
			return SoundIoFormatInvalid;
	}
}

static SampleFormat convertSampleFormat(SoundIoFormat format) {
	switch (format) {
		case SoundIoFormatS8:
			return SampleFormat::S8;        ///< Signed 8 bit
		case SoundIoFormatS16NE:
			return SampleFormat::S16; //< Signed 16 bit Native Endian
		case SoundIoFormatS24NE:
			return SampleFormat::S24_P32; ///< Signed 24 bit Little Endian using low three bytes in 32-bit word
		case SoundIoFormatS32NE:
			return SampleFormat::S32; ///< Signed 32 bit Native Endian
		case SoundIoFormatFloat32NE:
			return SampleFormat::FLOAT; ///< Float 32 bit Native Endian, Range -1.0 to 1.0
		default:
			return SampleFormat::S32; //???
	}
}

SoundIOOutput::SoundIOOutput(const ConfigBlock &block)
		: base(soundio_output_plugin, block)
		, soundio(0)
		, device(0)
		, outstream(0)
		, ring_buffer_size(0)
		, buffer(0)
		, ring_buffer(0) {
	// Create the output.
	soundio = soundio_create();
	if (!soundio) {
		throw std::runtime_error("Unable to create soundio device, out of memory");
	}
	FormatInfo(soundio_output_domain, "Created soundio object");
	
	// Connect it.
	int err;
	if ((err = soundio_connect(soundio))) {
		throw FormatRuntimeError("error connecting: %s", soundio_strerror(err));
	}
	FormatInfo(soundio_output_domain, "Connected to soundio object");
	
	// Flush events.
	soundio_flush_events(soundio);
	FormatInfo(soundio_output_domain, "Flushed soundio events");
	
	device = nullptr;
	
	const char *device_name = block.GetBlockValue("device", (const char *)nullptr);
	if (device_name) {
		// try specified device
		FormatInfo(soundio_output_domain, "Attempting to open device: %s", device_name);
		
		int device_count = soundio_output_device_count(soundio);
		if (device_count <= 0) {
			throw std::runtime_error("no output device found");
		}
		FormatInfo(soundio_output_domain, "Found %i devices", device_count);
		
		struct SoundIoDevice *next_device;
		for (int i = 0; i < device_count; i++) {
			next_device = soundio_get_output_device(soundio, i);
			if (next_device == nullptr) {
				throw std::runtime_error("out of memory");
			}
			FormatInfo(soundio_output_domain, "Device: %s", next_device->name);
			FormatInfo(soundio_output_domain, "  id: %s", next_device->id);
			FormatInfo(soundio_output_domain, "  raw: %i", next_device->is_raw);
			
			if (strcmp(device_name, next_device->name) == 0) {
				FormatInfo(soundio_output_domain, "Matched device: %s", next_device->id);
				device = next_device;
				break;
			}
			soundio_device_unref(next_device);
		}
		
	}
	
	if (!device) {
		// Use the default device.
		int default_out_device_index = soundio_default_output_device_index(soundio);
		if (default_out_device_index < 0) {
			throw std::runtime_error("no output device found");
		}
		device = soundio_get_output_device(soundio, default_out_device_index);
	}
	
	if (!device) {
		throw std::runtime_error("no output device found");
	}
	
	FormatInfo(soundio_output_domain, "Output device: %s\n", device->name);
	
	device_format.sample_rate = device->sample_rate_current;
	device_format.channels = device->current_layout.channel_count;
	if (device->is_raw) {
		device_format.format = SampleFormat::UNDEFINED;
	} else {
		device_format.format = SampleFormat::FLOAT;
	}
	FormatInfo(soundio_output_domain, "Current output stream format: %s\n", ToString(device_format).c_str());

	fade_out_time = 0.09;
	fade_in_time = 0.15;

	buffer = new uint8_t[READ_BUFFER_SIZE];
}

SoundIOOutput::~SoundIOOutput() {
	soundio_device_unref(device);
	soundio_destroy(soundio);
	delete [] buffer;
	delete ring_buffer;
}

#if 0
static void dump_layouts(struct SoundIoDevice *device) {
	struct SoundIoChannelLayout *nextLayout;
	
	for (int i = 0; i < device->layout_count; i += 1) {
		nextLayout = &device->layouts[i];
		FormatInfo(soundio_output_domain, "Layout %i: %s", i, nextLayout->name);
		for (int j = 0; j < nextLayout->channel_count; j += 1) {
			FormatInfo(soundio_output_domain, " - Channel %i: %s", j, soundio_get_channel_name(nextLayout->channels[j]));
		}
	}
}
#endif

#ifndef WIN32
static int
get_max_channels(struct SoundIoDevice *device)
{
	int max = 0;
	for (int i = 0; i < device->layout_count; i += 1) {
		SoundIoChannelLayout* nextLayout = &device->layouts[i];
		max = std::max(nextLayout->channel_count, max);
	}
	
	return max;
}
#endif

void
SoundIOOutput::Open(AudioFormat &audio_format)
{
	outstream = soundio_outstream_create(device);
	FormatInfo(soundio_output_domain, "Device latency between %f sec and %f sec.", device->software_latency_min, device->software_latency_max);
	if (device->software_latency_current != 0.0) {
		FormatInfo(soundio_output_domain, "Current device latency is %f sec.", device->software_latency_current);
	}
	
	if (!device->is_raw) {
		// Sample rate.
		if (device->sample_rate_current != (int)audio_format.sample_rate) {
			FormatInfo(soundio_output_domain, "%s is running in shared mode at sample rate %i so we'll use that.", device->name, device->sample_rate_current);
			audio_format.sample_rate = outstream->sample_rate = device->sample_rate_current;
		} else {
			outstream->sample_rate = audio_format.sample_rate;
		}
#ifdef _WIN32
		// At least on Windows, we seem to have to match the current format/channels.
		audio_format.format = convertSampleFormat(device->current_format);
		outstream->format = convertAudioFormat(audio_format.format);
		audio_format.channels = device->current_layout.channel_count;
#else
		// Format.
		outstream->format = convertAudioFormat(audio_format.format);
		if (outstream->format == SoundIoFormatInvalid) {
			throw FormatRuntimeError("could not handle sample format: %s", sample_format_to_string(audio_format.format));
		}
		// Layout.
		int numChannels = std::min(get_max_channels(device), (int)audio_format.channels);
		FormatInfo(soundio_output_domain, "Requested %d channels, and we're going to be using %d\n", audio_format.channels, numChannels);
		audio_format.channels = numChannels;
#endif
	} else {
		if (soundio_device_supports_sample_rate(device, audio_format.sample_rate)) {
			// Use the exact sample rate.
			outstream->sample_rate = audio_format.sample_rate;
		} else {
			// We'll need to re-sample.
			outstream->sample_rate = soundio_device_nearest_sample_rate(device, audio_format.sample_rate);
			audio_format.sample_rate = (uint32_t)outstream->sample_rate;
			FormatInfo(soundio_output_domain, "%s does not support sample rate %i, using nearest (%i)\n", device->name, audio_format.sample_rate, outstream->sample_rate);
		}
		outstream->format = convertAudioFormat(audio_format.format);
		audio_format.channels = device->current_layout.channel_count;
	}
	
	const struct SoundIoChannelLayout *layout = soundio_channel_layout_get_default(audio_format.channels);
	outstream->layout = *layout;
	outstream->write_callback = write_callback;
	outstream->error_callback = error_callback;
	outstream->underflow_callback = underflow_callback;
	outstream->userdata = this;
	
	// Open the device.
	int err;
	if ((err = soundio_outstream_open(outstream)))
		throw FormatRuntimeError("unable to open device: %s", soundio_strerror(err));
	FormatInfo(soundio_output_domain, "Opened output stream, %i Hz %s %s latency %.2f ms\n", outstream->sample_rate, outstream->layout.name, soundio_format_string(outstream->format), outstream->software_latency/audio_format.channels * 1000);
	if (outstream->layout_error)
		throw FormatRuntimeError("unable to set channel layout: %s\n", soundio_strerror(outstream->layout_error));
	
	// Create a ring buffer.
	ring_buffer_size = (unsigned long)(outstream->bytes_per_frame * outstream->sample_rate * 0.2);
	FormatInfo(soundio_output_domain, "Buffer size bytes per frame: %d, sample rate: %d, buffer size: %lu", outstream->bytes_per_frame, outstream->sample_rate, ring_buffer_size);
	ring_buffer = new boost::lockfree::spsc_queue<uint8_t>(ring_buffer_size);
		
	// Start device.
	if ((err = soundio_outstream_start(outstream))) {
		throw FormatRuntimeError("unable to start device: %s", soundio_strerror(err));
	}
	FormatInfo(soundio_output_domain, "Opened device.");
	paused = false;
	
	// always fade on start
	fade_level = 0.0;
}

void
SoundIOOutput::Close() noexcept
{
	Pause();
	soundio_outstream_destroy(outstream);
	outstream = nullptr;
	delete ring_buffer;
	ring_buffer = nullptr;
}

size_t
SoundIOOutput::Play(const void *chunk, size_t size)
{
	if (paused) {
		paused = false;
		//trigger fade in
		fade_level = 0.0;
	}
	return ring_buffer->push((const uint8_t *)chunk, size);
}

inline void
SoundIOOutput::Cancel()
{
    if (!paused) {
        Pause();
    }
}

inline bool
SoundIOOutput::Pause()
{
	if (!paused) {
		fade_level = 1.0;
		paused = true;

		for (int i = 0; i < 200; i++) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			if (fade_level <= 0.0) {
				break;
			}
		}
	}
	return true;
}

std::chrono::steady_clock::duration
SoundIOOutput::Delay() const noexcept
{
	// If we're paused, let's sleep here, otherwise because there
	// is write space available, we'll get busy called. If we return
	// a delay because we're paused, we won't unpause.
	//
	if (paused) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return ring_buffer->write_available()
		   ? std::chrono::steady_clock::duration::zero()
		   : std::chrono::milliseconds(25);
}

AudioFormat
SoundIOOutput::DeviceFormat() {
	return device_format;
}
	
double
SoundIOOutput::Latency() {
    size_t sample_count = ring_buffer->read_available();
	double buffer_latency = (double)(sample_count) / outstream->bytes_per_frame / device_format.sample_rate;
	return buffer_latency + outstream->software_latency;
}

typedef AudioOutputWrapper<SoundIOOutput> Wrapper;

const struct AudioOutputPlugin soundio_output_plugin = {
	"soundio",
	nullptr,
	&Wrapper::Init,
	&Wrapper::Finish,
	nullptr,
	nullptr,
	&Wrapper::Open,
	&Wrapper::Close,
	&Wrapper::Delay,
	nullptr,
	&Wrapper::Play,
	nullptr,
	&Wrapper::Cancel,
	&Wrapper::Pause,
	&Wrapper::DeviceFormat,
	&Wrapper::Latency,
	&soundio_mixer_plugin,
};

