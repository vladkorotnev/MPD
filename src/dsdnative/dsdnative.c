#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "dsdnative.h"

/* 09-Dec-11 Jurgen Kramer
 * dsd2pcm.c converted to output native DSD (packed according to dCS spec) instead of PCM
 * Tested with Mytek Stereo192-DSD DAC (using Linux snd-dice firewire driver)
 * 24-bit dCS spec will be put in SAMPLE_FORMAT_S32
 *
 * 22-Jan-12 Jurgen Kramer
 * Add support for DSF (Sony) format
 * 23-Feb-12 Jurgen Kramer
 * Added a workaround for some DSF files, prevent hang/pop at the end of the file
 */

/* Currently only stereo streams are supported */

/* translate DSF formatted stream to DSD-over-PCM 
 * 
 * DSF format uses a interleaved channel format in
 * 4096 byte blocks
 *
 * E.g. for stereo files:
 * block 1: 4096 bytes left channel data
 * block 2: 4096 bytes right channel data
 * block 3: 4096 bytes left channel data
 * block 4 ...
 * 
 * Last blocks are padded with zeros if not completely used
 *
 */

#define MSBDAC 1
#if MSBDAC
#define DSD_FLAG0 0x05
#define DSD_FLAG1 0xFA
#else
#define DSD_FLAG0 0xAA
#define DSD_FLAG1 0xAA
#endif

static unsigned char dsdMarker = DSD_FLAG0;

#define SWITCH_DSD_MARKER do{\
                             if(dsdMarker == DSD_FLAG0)\
                                  dsdMarker = DSD_FLAG1;\
                             else\
                                  dsdMarker = DSD_FLAG0;\
                          }while(0);


static unsigned char bitreverse[256];

extern void preapre_reverse_table() {
    int t,e,m;
    for (t=0, e=0; t<256; ++t) {    /* Create bitreverse table */
        bitreverse[t] = e;
        for (m=128; m && !((e^=m)&m); m>>=1)
            ;
    }
}


extern void dsdnative_translate_dsf(
	size_t samples,
	const unsigned char *src, unsigned char *dst, unsigned int dsdsampleformat, uint32_t dsdiff_dsf_bitssample )
{
	unsigned char left1, left2, right1, right2;
	int i;

    /// 16384 from .dsdiff_nativeplayback_plugin.c 
    /// in dsdiff_nativeplayback_plugin, buffer size which is alsot dst size
    const unsigned char *dstEnd   = dst + 16384 - 1;
    
    if (1) {
		/// some case due to tags at the end samples might not 8192
        const size_t halfSize = (samples / 2) - 1;
        const unsigned char *srcLeft  = src;
        const unsigned char *srcRight = src + 4096;

        unsigned char* dstLeft  = dst;
        unsigned char* dstRight = dst + 4;

		/// since samples size is not 8192, we need to use this
        while ( samples > halfSize )
        {
            /// process data for both left and right channel at the same time
            for (i=0; i<2048; i++) {
                left1 = ( *srcLeft ++);
                left2 = ( *srcLeft ++);

                right1 = ( *srcRight ++);
                right2 = ( *srcRight ++);

                if ( dsdiff_dsf_bitssample == 1) {
                    left1 = bitreverse[left1];
                    left2 = bitreverse[left2];

                    right1 = bitreverse[right1];
                    right2 = bitreverse[right2];
                }

                if (dsdsampleformat == 24) {
                    ( *dstLeft ++ )= left2;
                    ( *dstLeft ++ )= left1;
                    ( *dstLeft ++ )= dsdMarker;  /* Must stay here for DAC to lock to DSD! */
                    ( *dstLeft ++ )= 0x00;   /* Ignored byte, leave at 0x00 */

                    ( *dstRight ++ )= right2;
                    ( *dstRight ++ )= right1;
                    ( *dstRight ++ )= dsdMarker;  /* Must stay here for DAC to lock to DSD! */
                    ( *dstRight ++ )= 0x00;  /* Ignored byte, leave at 0x00 */
                }
                else {
                    ( *dstLeft ++ )= 0x00;   /* Ignored byte, leave at 0x00 */
                    ( *dstLeft ++ )= left2;
                    ( *dstLeft ++ )= left1;
                    ( *dstLeft ++ )= dsdMarker;  /* Must stay here for DAC to lock to DSD! */

                    ( *dstRight ++ )= 0x00;  /* Ignored byte, leave at 0x00 */
                    ( *dstRight ++ )= right2;
                    ( *dstRight ++ )= right1;
                    ( *dstRight ++ )= dsdMarker;  /* Must stay here for DAC to lock to DSD! */
                }

                dstLeft  += (ptrdiff_t) 4;
                dstRight += (ptrdiff_t) 4;
                SWITCH_DSD_MARKER
            }
            if (samples >= 4 * 2048)
                samples -= 4 * 2048;
            else {
                g_message(" Unexpected case, so we reset samples to 0");
                samples = 0;
            }

            dstLeft  -= (ptrdiff_t) 4;
            dstRight -= (ptrdiff_t) 4;


            if (dstLeft - 4 > dstEnd)
            {
                g_debug("-------------dsf out of bounds-----------");
                return;
            }
        }
    }
    else {
	    if ( dsdiff_dsf_bitssample == 1)			/* 1 = LSB first, 8 = MSB first, DSD-over-USB wants MSB first */
    	{
    		int t,e,m;
    		for (t=0, e=0; t<256; ++t)			/* Create bitreverse table */
    		{
    			bitreverse[t] = e;
    			for (m=128; m && !((e^=m)&m); m>>=1)
    				;
    		}
    	}
    
    	/* DSD packed sample format dCS spec (24-bit): 
    	 *	0xAA LSB left --- MSB left  -> 24-bits
    	 *	0xAA LSB Right ---MSB Right -> 24-bits
    	 */
    
    	while ( samples > 4095)					/* This will skip the last few samples to prevent a possible hang/pop sounds */
    	{
    		/* Process data for the left channel */
    
    		for (i=0; i<2048; i++)
    		{
    			left1 = ( *src ++ );
    			left2 = ( *src ++ );
    	
    			if ( dsdiff_dsf_bitssample == 1)	/* bitreverse needed? */
    			{
    				left1 = bitreverse[left1];
    				left2 = bitreverse[left2];
    			}
    
    			if (dsdsampleformat == 24)
    			{
    				( *dst ++ )= left2;
    				( *dst ++ )= left1;		
    				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
    				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
    			}
    			else
    			{
    				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
    				( *dst ++ )= left2;
    				( *dst ++ )= left1;
    				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
    			}
    			dst += (ptrdiff_t) 4;
    			samples -= 2;
    			SWITCH_DSD_MARKER
    		}
    
    		dst -= (ptrdiff_t) 16380;
    	
    		/* Process data for the right channel */
    	
    		for (i=0; i<2048; i++)
    		{
    			right1 = ( *src ++ );
    			right2 = ( *src ++ );
    
    			if ( dsdiff_dsf_bitssample == 1)
    			{
    				right1 = bitreverse[right1];
    				right2 = bitreverse[right2];
    			}
    
    			if (dsdsampleformat == 24)
    			{
    				( *dst ++ )= right2;
    				( *dst ++ )= right1;		
    				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
    				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
    			}
    			else
    			{
    				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
    				( *dst ++ )= right2;
    				( *dst ++ )= right1;		
    				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
    			}	
    			dst += (ptrdiff_t) 4;
    			samples -= 2;
    			SWITCH_DSD_MARKER
    		}
    		dst -= (ptrdiff_t) 4;
    	}
    }
}

/* translate DFF formatted stream to DSD-over-PCM */

extern void dsdnative_translate_dff(
	size_t samples,
	const unsigned char *src, unsigned char *dst, unsigned int dsdsampleformat )
{
	unsigned char left1, left2, right1, right2;
	/* DSD packed sample format dCS spec (24-bit): 
	 *	0xAA LSB left --- MSB left  -> 24-bits
	 *	0xAA LSB Right ---MSB Right -> 24-bits
	 */
	while (samples > 0)
	  {
	  if(samples != 2)
		{
			left1 = ( *src ++ );
			right1 = ( *src ++ );
			left2 = ( *src ++ );
			right2 = ( *src ++ );
		
			if (dsdsampleformat == 24)
			{
			/* This 'works' for SAMPLE_FORMAT_S24_P32 when convert back to SAMPLE_FORMAT_S32 by mpd */
				( *dst ++ )= left2;
				( *dst ++ )= left1;		
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 

				( *dst ++ )= right2;
				( *dst ++ )= right1;
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
			}
			else //32
			{
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
				( *dst ++ )= left2;
				( *dst ++ )= left1;		
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
	
				( *dst ++ )= 0x00;		/* Ignore byte, leave at 0x00 */
				( *dst ++ )= right2;
				( *dst ++ )= right1;
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
			}
			samples = samples - 4;
			SWITCH_DSD_MARKER
		}
	  else				/* Process last two samples */
		{
			left1 = ( *src ++ );
			right1 = ( *src ++ );

			if (dsdsampleformat == 24)
			{
				/* This 'works' for SAMPLE_FORMAT_S24_P32 when converted back to SAMPLE_FORMAT_S32 by mpd */
				( *dst ++ )= 0x00;
				( *dst ++ )= left1;		
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 

				( *dst ++ )= 0x00;
				( *dst ++ )= right1;
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
			}
			else
			{
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */ 
				( *dst ++ )= 0x00;		/* Last 2 samples: no left2 sample */
				( *dst ++ )= left1;		
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
	
				( *dst ++ )= 0x00;		/* Ignored byte, leave at 0x00 */
				( *dst ++ )= 0x00;		/* Last 2 samples: no right2 sample */
				( *dst ++ )= right1;
				( *dst ++ )= dsdMarker;		/* Must stay here for DAC to lock to DSD! */
			}
			SWITCH_DSD_MARKER
			samples = 0;
		}
	}

}

