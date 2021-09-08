#include "video_x264.h"

// h.264 encoder lib
//  this requires stdint.h first or else it complains...
#include <stdint.h>
#include <x264.h>

// for malloc
#include <stdlib.h>
// for fprintf
#include <stdio.h>

// structure definition for the encoder (private data)
struct encoder_video {
	// user callback to generate more audio
	int (* callback)(unsigned char ** frame);

	// buffer we will write to
	unsigned char * buffer;

	// x264 objects
	x264_t * encoder;
	x264_picture_t picture;
};

// init
struct encoder_video * video_x264_create(const unsigned int width, const unsigned int height, const unsigned int framerate, const unsigned int bitrate, int (* callback)(unsigned char ** frame), unsigned char * destination)
{
	// create a structure
	struct encoder_video * e = malloc(sizeof(struct encoder_video));

	if (e == NULL) {
		perror("librtmpcast: ERROR: video_x264::video_x264_create: malloc() returned NULL");
		return NULL;
	}

	// store the callback
	e->callback = callback;
	e->buffer = destination;

	// return code handler
	int ret;

	// Initialize the x264 encoder
	//  First set up the parameters struct
	//  TODO: we probably want to allow other things in here
	x264_param_t x_p;
	ret = x264_param_default_preset(&x_p, "veryfast", "zerolatency");
	if (ret) {
		fprintf(stderr, "librtmpcast: ERROR: x264_param_default_preset returned %d\n", ret);
		x264_param_cleanup(&x_p);
		free(e);
		return NULL;
	}
	//x_p.i_log_level = X264_LOG_INFO;
	x_p.i_threads = 1;
	x_p.i_width = width;
	x_p.i_height = height;
	x_p.i_fps_num = framerate;
	x_p.i_fps_den = 1;
	x_p.i_keyint_max = framerate * 4; // Twitch likes keyframes every 4 sec or less

	// Enable intra refresh instead of IDR
	//x_p.b_intra_refresh = 1;

	// Rate control - use CBR not CRF.
	//  Chosen by setting bitrate and vbv_max_bitrate to same value.
	x_p.rc.i_rc_method = X264_RC_ABR;
	x_p.rc.i_bitrate = bitrate;
	x_p.rc.i_vbv_max_bitrate = bitrate;
	x_p.rc.i_vbv_buffer_size = bitrate;

	// Control x264 output for muxing
	x_p.b_aud = 0; // do not generate Access Unit Delimiters
	x_p.b_repeat_headers = 1; // Do not put SPS/PPS before each keyframe.
	x_p.b_annexb = 0; // Annex B uses startcodes before NALU, but we want sizes

	// constraints
	//  TODO: this should be switchable?  baseline / main / high
	ret = x264_param_apply_profile(&x_p, "baseline");
	//ret = x264_param_apply_profile(&x_p, "high");
	if (ret) {
		fprintf(stderr, "librtmpcast: ERROR: x264_param_apply_profile returned %d\n", ret);
		x264_param_cleanup(&x_p);
		free(e);
		return NULL;
	}

	/* *************************************************** */
	// All done setting up params!  Let's open an encoder
	e->encoder = x264_encoder_open(&x_p);
	// can free the param struct now
	x264_param_cleanup(&x_p);
	// check the return value from video.encoder creation
	if (! e->encoder) {
		fputs("librtmpcast: ERROR: failed to create x264 encoder\n", stderr);
		free(e);
		return NULL;
	}

	// These are the two picture structs.  Input must be alloc()
	//  Output will be created by the encode process
	ret = x264_picture_alloc(&e->picture, X264_CSP_I420, width, height);
	if (ret) {
		fprintf(stderr, "librtmpcast: ERROR: x264_picture_alloc returned %d\n", ret);
		x264_encoder_close(e->encoder);
		free(e);
		return NULL;
	}

	return e;
}

int video_x264_init(const struct encoder_video * e)
{
	// write the h.264 header now
	x264_nal_t * pp_nal;
	int pi_nal;

	int header_size = x264_encoder_headers(e->encoder, &pp_nal, &pi_nal);

	// TODO: identify SPS / PPS instead of assuming
	unsigned char * sps = pp_nal[0].p_payload + 4;
	const unsigned short sps_length = pp_nal[0].i_payload;
	unsigned char * pps = pp_nal[1].p_payload + 4;
	const unsigned short pps_length = pp_nal[1].i_payload;

	// write the decoder config record, the initial SPS and PPS
	// AVCDecoder record - some of this data comes out of the SPS for this block
	unsigned char * p = e->buffer;
	*p = 0x01;	// version
	*(p + 1) = sps[1];	// Required profile ID
	*(p + 2) = sps[2];	// Profile compatibility
	*(p + 3) = sps[3];	// AVC Level (3.0)
	*(p + 4) = 0b11111100 | 0b11;	// NAL lengthSizeMinusOne (4 bytes)
	*(p + 5) = 0b11100000 | 1;	// number of SPS sets
	p += 6;

	// write the SPS - length (uint16), then data
	*p = sps_length >> 8 & 0xFF; p++;
	*p = sps_length & 0xFF; p++;
	memcpy(p, sps, sps_length);
	p += sps_length;

	// write the PPS now
	*p = pps_length >> 8 & 0xFF; p++;
	*p = pps_length & 0xFF; p++;
	memcpy(p, pps, pps_length);
	p += pps_length;

	return p - e->buffer;

}

struct video_return_t video_x264_update(const struct encoder_video * e)
{
	// update
	e->callback(e->picture.img.plane);

	/* Encode an x264 frame */
	x264_nal_t * nals;
	int i_nals;
	x264_picture_t pic_out;

	struct video_return_t ret;
	ret.size = x264_encoder_encode(e->encoder, &nals, &i_nals, &e->picture, &pic_out);
	ret.keyframe = pic_out.b_keyframe;
	//ret.dts = pic_out.i_dts;
	
	if (ret.size < 0) {
		// error in encoding
		fputs("Error when encoding frame\n", stderr);
	} else if (ret.size > 0) {
		// write every NALU to the packet for this pic
		//  x264 guarantees all p_payload are sequential
		// TODO: would be nice if we could tell the payload where to go!
		//  or at a minimum maybe skip disposable NALs
		memcpy(e->buffer, nals[0].p_payload, ret.size);
	}

	return ret;
}

void video_x264_close(struct encoder_video * e)
{
	/*
	   while( x264_encoder_delayed_frames( h ) )
	   {
	   i_frame_size = x264_encoder_encode( h, &nal, &i_nal, NULL, &pic_out );
	   if( i_frame_size < 0 )
	   goto fail;
	   else if( i_frame_size )
	   {
	   if( !fwrite( nal->p_payload, i_frame_size, 1, stdout ) )
	   goto fail;
	   }
	   }
	 */

	x264_picture_clean(&e->picture);
	x264_encoder_close(e->encoder);
	free(e);
}
