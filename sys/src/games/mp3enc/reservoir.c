/*
 *	bit reservoir source file
 *
 *	Copyright (c) 1999 Mark Taylor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id: reservoir.c,v 1.14 2001/01/05 15:20:33 aleidinger Exp $ */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include "util.h"
#include "reservoir.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/*
  ResvFrameBegin:
  Called (repeatedly) at the beginning of a frame. Updates the maximum
  size of the reservoir, and checks to make sure main_data_begin
  was set properly by the formatter
*/

/*
 *  Background information:
 *
 *  This is the original text from the ISO standard. Because of 
 *  sooo many bugs and irritations correcting comments are added 
 *  in brackets []. A '^W' means you should remove the last word.
 *
 *  1) The following rule can be used to calculate the maximum 
 *     number of bits used for one granule [^W frame]: 
 *     At the highest possible bitrate of Layer III (320 kbps 
 *     per stereo signal [^W^W^W], 48 kHz) the frames must be of
 *     [^W^W^W are designed to have] constant length, i.e. 
 *     one buffer [^W^W the frame] length is:
 *
 *         320 kbps * 1152/48 kHz = 7680 bit = 960 byte
 *
 *     This value is used as the maximum buffer per channel [^W^W] at 
 *     lower bitrates [than 320 kbps]. At 64 kbps mono or 128 kbps 
 *     stereo the main granule length is 64 kbps * 576/48 kHz = 768 bit
 *     [per granule and channel] at 48 kHz sampling frequency. 
 *     This means that there is a maximum deviation (short time buffer 
 *     [= reservoir]) of 7680 - 2*2*768 = 4608 bits is allowed at 64 kbps. 
 *     The actual deviation is equal to the number of bytes [with the 
 *     meaning of octets] denoted by the main_data_end offset pointer. 
 *     The actual maximum deviation is (2^9-1)*8 bit = 4088 bits 
 *     [for MPEG-1 and (2^8-1)*8 bit for MPEG-2, both are hard limits].
 *     ... The xchange of buffer bits between the left and right channel
 *     is allowed without restrictions [exception: dual channel].
 *     Because of the [constructed] constraint on the buffer size
 *     main_data_end is always set to 0 in the case of bit_rate_index==14, 
 *     i.e. data rate 320 kbps per stereo signal [^W^W^W]. In this case 
 *     all data are allocated between adjacent header [^W sync] words 
 *     [, i.e. there is no buffering at all].
 */

int
ResvFrameBegin(lame_global_flags *gfp,III_side_info_t *l3_side, int mean_bits, int frameLength )
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int fullFrameBits;
    int resvLimit;
    int maxmp3buf;

/*
 *  Meaning of the variables:
 *      resvLimit: (0, 8, ..., 8*255 (MPEG-2), 8*511 (MPEG-1))
 *          Number of bits can be stored in previous frame(s) due to 
 *          counter size constaints
 *      maxmp3buf: ( ??? ... 8*1951 (MPEG-1 and 2), 8*2047 (MPEG-2.5))
 *          Number of bits allowed to encode one frame (you can take 8*511 bit 
 *          from the bit reservoir and at most 8*1440 bit from the current 
 *          frame (320 kbps, 32 kHz), so 8*1951 bit is the largest possible 
 *          value for MPEG-1 and -2)
 *      fullFrameBits:
 *
 *      mean_bits:
 *
 *      frameLength:
 *
 *      gfc->ResvMax:
 *
 *      gfc->ResvSize:
 *
 *      l3_side->resvDrain_pre:
 *
 */

    /* main_data_begin has 9 bits in MPEG-1, 8 bits MPEG-2 */
    resvLimit = (gfp->version==1) ? 8*511 : 8*255 ;


    /* maximum allowed frame size */
    maxmp3buf = (gfp->strict_ISO) ? 8*960 : 8*2047;

    if ( frameLength > maxmp3buf ||  gfp->disable_reservoir ) {
	gfc->ResvMax = 0;
    } else {
	gfc->ResvMax = maxmp3buf - frameLength;
	if ( gfc->ResvMax > resvLimit )
	  gfc->ResvMax = resvLimit;
    }

    fullFrameBits = mean_bits * gfc->mode_gr + Min ( gfc->ResvSize, gfc->ResvMax );
    
    if ( gfp->strict_ISO  &&  fullFrameBits > maxmp3buf )
        fullFrameBits = maxmp3buf;

    assert ( 0 == gfc->ResvMax % 8 );
    assert ( gfc->ResvMax >= 0 );

    l3_side->resvDrain_pre = 0;

    if ( gfc->pinfo != NULL ) {
        gfc->pinfo->mean_bits = mean_bits / 2;  /* expected bits per channel per granule [is this also right for mono/stereo, MPEG-1/2 ?] */
        gfc->pinfo->resvsize  = gfc->ResvSize;
    }

    return fullFrameBits;
}


/*
  ResvMaxBits
  returns targ_bits:  target number of bits to use for 1 granule
         extra_bits:  amount extra available from reservoir
  Mark Taylor 4/99
*/
void ResvMaxBits(lame_global_flags *gfp, int mean_bits, int *targ_bits, int *extra_bits)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  int add_bits;
  int full_fac;
  
  *targ_bits = mean_bits ;

  /* extra bits if the reservoir is almost full */
  full_fac=9;
  if (gfc->ResvSize > ((gfc->ResvMax * full_fac) / 10)) {
    add_bits= gfc->ResvSize-((gfc->ResvMax * full_fac) / 10);
    *targ_bits += add_bits;
  }else {
    add_bits =0 ;
    /* build up reservoir.  this builds the reservoir a little slower
     * than FhG.  It could simple be mean_bits/15, but this was rigged
     * to always produce 100 (the old value) at 128kbs */
    /*    *targ_bits -= (int) (mean_bits/15.2);*/
    if (!gfp->disable_reservoir) 
      *targ_bits -= .1*mean_bits;
  }


  /* amount from the reservoir we are allowed to use. ISO says 6/10 */
  *extra_bits =
    (gfc->ResvSize  < (gfc->ResvMax*6)/10  ? gfc->ResvSize : (gfc->ResvMax*6)/10);
  *extra_bits -= add_bits;

  if (*extra_bits < 0) *extra_bits=0;


}

/*
  ResvAdjust:
  Called after a granule's bit allocation. Readjusts the size of
  the reservoir to reflect the granule's usage.
*/
void
ResvAdjust(lame_internal_flags *gfc,gr_info *gi, III_side_info_t *l3_side, int mean_bits )
{
  gfc->ResvSize += (mean_bits / gfc->channels_out) - gi->part2_3_length;
#if 0
  printf("part2_3_length:  %i  avg=%i  incres: %i  resvsize=%i\n",gi->part2_3_length,
	 mean_bits/gfc->channels_out,
mean_bits/gfc->channels_out-gi->part2_3_length,gfc->ResvSize);
#endif
}


/*
  ResvFrameEnd:
  Called after all granules in a frame have been allocated. Makes sure
  that the reservoir size is within limits, possibly by adding stuffing
  bits.
*/
void
ResvFrameEnd(lame_internal_flags *gfc, III_side_info_t *l3_side, int mean_bits)
{
    int stuffingBits;
    int over_bits;


    /* just in case mean_bits is odd, this is necessary... */
    if ( gfc->channels_out == 2  &&  (mean_bits & 1) )
	gfc->ResvSize += 1;

    stuffingBits=0;
    l3_side->resvDrain_post = 0;
    l3_side->resvDrain_pre = 0;

    /* we must be byte aligned */
    if ( (over_bits = gfc->ResvSize % 8) != 0 )
	stuffingBits += over_bits;


    over_bits = (gfc->ResvSize - stuffingBits) - gfc->ResvMax;
    if (over_bits > 0) {
      assert ( 0 == over_bits % 8 );
      assert ( over_bits >= 0 );
      stuffingBits += over_bits;
    }


#define NEW_DRAINXX
#ifdef NEW_DRAIN
    /* drain as many bits as possible into previous frame ancillary data
     * In particular, in VBR mode ResvMax may have changed, and we have
     * to make sure main_data_begin does not create a reservoir bigger
     * than ResvMax  mt 4/00*/
  {
    int mdb_bytes = Min(l3_side->main_data_begin*8,stuffingBits)/8;
    l3_side->resvDrain_pre += 8*mdb_bytes;
    stuffingBits -= 8*mdb_bytes;
    gfc->ResvSize -= 8*mdb_bytes;
    l3_side->main_data_begin -= mdb_bytes;


    /* drain just enough to be byte aligned.  The remaining bits will
     * be added to the reservoir, and we will deal with them next frame.
     * If the next frame is at a lower bitrate, it may have a larger ResvMax,
     * and we will not have to waste these bits!  mt 4/00 */
    assert ( stuffingBits >= 0 );
    l3_side->resvDrain_post += (stuffingBits % 8);
    gfc->ResvSize -= stuffingBits % 8;
  }
#else
    /* drain the rest into this frames ancillary data*/
    l3_side->resvDrain_post += stuffingBits;
    gfc->ResvSize -= stuffingBits;
#endif

    return;
}

