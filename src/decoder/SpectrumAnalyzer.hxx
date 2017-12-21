//
//  SpectrumAnalyzer.hpp
//  mpd
//
//  Created by Ryan Walklin on 12/10/17.
//  Copyright © 2017 Ryan Walklin. All rights reserved.
//

#ifndef SpectrumAnalyzer_hxx
#define SpectrumAnalyzer_hxx

#include <AudioFormat.hxx>
#include <fftw3.h>
#include <vector>

struct MusicChunk;

class SpectrumAnalyzer {
	
private:
	AudioFormat format;
	
    int fft_window_size;
    int fft_buffer_size;
    int fft_buffer_position;

	fftwf_plan p;
	float *buffer;
	float *in;
	float *magnitude;
	fftwf_complex *out;
	
	
	std::vector<float> bins;
		
public:
	SpectrumAnalyzer(AudioFormat _format);
	~SpectrumAnalyzer();
	
	// add chunk and return current bins
	void Analyze(MusicChunk *chunk, float gain);
	
	// reset
	void Reset();
	
};

#endif /* SpectrumAnalyzer_hpp */
