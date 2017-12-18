#ifndef DSDNATIVE_H_INCLUDED
#define DSDNATIVE_H_INCLUDED

#include <stddef.h>
#include <string.h>

#include <stdint.h>			/* for uint32_t */

extern void preapre_reverse_table();

/**
 * "translates" a stream of octets to a stream 
 * of packed DSD formatted pcm
 * 
 * @param samples -- number of octets/samples to "translate"
 * @param src -- pointer to first octet (input)
 * @param dst -- pointer to first sample(output) 
 * @param dsdsampleformat -- 32 or 24-bit output sample format
 * Version for handling DFF (Philips) format
 */

extern void dsdnative_translate_dff(size_t samples,
	const unsigned char *src, unsigned char *dst, unsigned int dsdsampleformat );

/**
 * "translates" a stream of octets to a stream 
 * of packed DSD formatted pcm
 * 
 * @param samples -- number of octets/samples to "translate"
 * @param src -- pointer to first octet (input)
 * @param dst -- pointer to first 32-bits sample(output)
 * @param dsdsampleformat -- 32 or 24-bit output sample format 
 * @param dsdiff_dsf_bitssample -- 1 = LSB, 8 = MSB
 *
 * Version for handling DSF (Sony) format
 */

extern void dsdnative_translate_dsf(size_t samples,
	const unsigned char *src, unsigned char *dst, unsigned int dsdsampleformat, uint32_t dsdiff_dsf_bitssample );

#endif /* include guard DSDNATIVE_H_INCLUDED */

