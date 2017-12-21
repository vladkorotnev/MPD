//
//  SpectrumAnalyzer.cpp
//  mpd
//
//  Created by Ryan Walklin on 12/10/17.
//  Copyright © 2017 Ryan Walklin. All rights reserved.
//

#include "SpectrumAnalyzer.hxx"

#include "config.h"
#include "MusicChunk.hxx"

#include <complex.h>
#include <string.h>

#define BIN_COUNT 24

template <typename T>
T clamp(const T& n, const T& lower, const T& upper) {
    return std::max(lower, std::min(n, upper));
}

void range (float a, float b, float step, std::vector<float> &ranged) {
	assert(a != NaN);
	assert(b != NaN);
	assert(step != NaN);
	assert(step != 0);
	
	if (step == 1) {
		if (a > b)
			step = -step;
	}
	else if ((a > a && step > 0) || (a < b && step < 0)) {
		ranged.clear();
		return;
	}
	
	int count = ceil(abs((b - a) / step));
	
	for (int i = 0; i < count; i++) {
		ranged.push_back(a + step * i);
	}
}

void linspace (float a, float b, int n, std::vector<float> &ranged) {
	
	float every = (b-a)/(n-1);
	
	range(a, b, every, ranged);
	
	if (ranged.size() > 0 && (int)ranged.size() < n) {
		ranged.push_back(b);
	}
}

void logspace (float a, float b, int n, std::vector<float> &ranged) {
	
	linspace(a, b, n, ranged);
	assert(ranged.size() == n);
	
	for (int i = 0; i < (int)ranged.size(); i++) {
		ranged[i] = powf(10, ranged[i]);
	}
}

void hann_window(float *buffer, int length) {
	for (int i = 0; i < length; i++) {
		float multiplier = 0.5 * (1 - cosf(2*M_PI*i/(length-1)));
		buffer[i] *= multiplier;
	}
}


SpectrumAnalyzer::SpectrumAnalyzer(AudioFormat _format)
        :format(_format),
		fft_window_size(1),
        fft_buffer_position(0)
{
    int min_buffer_size = format.sample_rate / 12; // Minimum size for 25Hz frequency
    
    while (fft_window_size < min_buffer_size) {
        fft_window_size <<= 1;
    }
    fft_buffer_size = fft_window_size * 1.5;
    
	buffer = (float *) fftwf_malloc(sizeof(float) * fft_buffer_size);
	in = (float *) fftwf_malloc(sizeof(float) * fft_window_size);
	
	out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * fft_window_size);
	magnitude = (float *) fftwf_malloc(sizeof(float) * fft_window_size/2);
	p = fftwf_plan_dft_r2c_1d(fft_window_size, in, out, FFTW_ESTIMATE);
	
	// Start at 65Hz to approximate A440 octaves (like a boss).
	logspace(log10f(65), log10f(5000), BIN_COUNT, bins);
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
	// cleanup
	fftwf_destroy_plan(p);
	fftwf_free(in);
	fftwf_free(out);
}

float a_weight(int frequency) {
	float f = frequency;
	float f2 = f*f;
	return 1.2588966 * 148840000 * f2*f2 / ((f2 + 424.36) * sqrtf((f2 + 11599.29) * (f2 + 544496.41)) * (f2 + 148840000));
}

float m_weight(int frequency) {
	float f = frequency;
	float f2 = f*f;
	
	float h1 = -4.737338981378384e-24*f2*f2*f2 + 2.043828333606125e-15*f2*f2 - 1.363894795463638e-7*f2 + 1;
	float h2 = 1.306612257412824e-19*f2*f2*f - 2.118150887518656e-11*f2*f + 5.559488023498642e-4*f;
	
	return 8.128305161640991 * 1.246332637532143e-4 * f / sqrtf(h1*h1 + h2*h2);
}

void SpectrumAnalyzer::Analyze(MusicChunk *chunk, float gain) {
	
	float *input = (float *)chunk->data;
	
	const uint8_t channels = format.channels;
	const int frame_count = chunk->length / sizeof(float) / channels;
	
	// how many frames do we have?
	int available = fft_buffer_size - fft_buffer_position;
    const int frames_to_copy = std::min(available, frame_count);

	if (available >= frames_to_copy) {
		// append this frame
		int next_frame = 0;
		int next_in = fft_buffer_position;
		while (next_frame < frames_to_copy) {
			buffer[next_in] = 0;
			for (int i = 0; i < channels; i++) {
				buffer[next_in] += input[next_frame*channels+i];
			}
			buffer[next_in++] /= channels;
			next_frame++;
		}
		fft_buffer_position += frames_to_copy;
	}
	
	if (fft_buffer_position < fft_buffer_size) {
		return;
	}
    
    // copy first chunk to in
    memcpy(in, buffer, fft_window_size*sizeof(float));
	
	// window
	hann_window(in, fft_window_size);
    
	// do first fft
	fftwf_execute(p);
    
    // Normalise
    float fft_norm_factor = 1.0/(2*fft_window_size);
	for (int i = 1; i <= fft_window_size / 2; i++) {
		out[i][0] *= fft_norm_factor;
		out[i][1] *= fft_norm_factor;
	}
    //vDSP_vsmul(fftHelperRef->complexA.realp, 1, &mFFTNormFactor, fftHelperRef->complexA.realp, 1, numSamples/2);
    //vDSP_vsmul(fftHelperRef->complexA.imagp, 1, &mFFTNormFactor, fftHelperRef->complexA.imagp, 1, numSamples/2);
    
    // get RMS magnitudes
    for (int i = 1; i <= fft_window_size / 2; i++) {
        magnitude[i-1] = sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
    }
    
    // copy second chunk
    memcpy(in, buffer+(int)(0.5*fft_window_size), fft_window_size*sizeof(float));
	
	// window
	hann_window(in, fft_window_size);
	
    // reset buffer
	memmove(buffer, buffer+(int)(0.5*fft_window_size), fft_window_size*sizeof(float));
    fft_buffer_position = fft_window_size;
    
    // append any extra from this chunk
    available = fft_buffer_size - fft_buffer_position;
    
    if (available >= (frame_count - frames_to_copy)) {
        // append remainder of frame
        int next_frame = frames_to_copy;
        int next_in = fft_buffer_position;
        while (next_frame < frame_count) {
            buffer[next_in] = 0;
            for (int i = 0; i < channels; i++) {
                buffer[next_in] += input[next_frame*channels+i];
            }
            buffer[next_in++] /= channels;
            next_frame++;
        }
        fft_buffer_position += frames_to_copy;
    }
    
    // second fft
    fftwf_execute(p);
    
    // normalise
	for (int i = 1; i <= fft_window_size / 2; i++) {
		out[i][0] *= fft_norm_factor;
		out[i][1] *= fft_norm_factor;
	}

    // average RMS magnitudes
    for (int i = 1; i <= fft_window_size / 2; i++) {
        magnitude[i-1] += sqrtf(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
        magnitude[i-1] /= 2;
    }

    // m-weight and bin
	float mag_step = format.sample_rate / fft_window_size / 2;
	std::vector<float> binned_mags(BIN_COUNT);
	
	for (int i = 0; i < fft_window_size / 2; i++) {
		const float freq = i*mag_step + mag_step/2;
		magnitude[i] *= m_weight(freq);
		
		for (int j = 0; j < BIN_COUNT; j++) {
			if (freq >= bins[j] && ((j + 1) == BIN_COUNT || freq < bins[j+1])) {
				binned_mags[j] = fmaxf(binned_mags[j], magnitude[i]);
			}
		}
	}
	
	// convert to dB
	for (int i = 0; i < BIN_COUNT; i++) {
		if (binned_mags[i] > 0) {
			binned_mags[i] = 20 * log10f(binned_mags[i]);
        } else {
            binned_mags[i] = -96;
        }
        binned_mags[i] = clamp<float>(binned_mags[i] + gain, -96, 0);
		
	}
	chunk->bins = binned_mags;
}

void SpectrumAnalyzer::Reset() {
	// clear buffer
	fft_buffer_position = 0;
}
