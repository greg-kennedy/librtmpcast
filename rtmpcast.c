/* ***************************************************
librtmpcast:
C library for streaming to Twitch (or other RTMP-based services)

Greg Kennedy 2021
*************************************************** */

#include "rtmpcast.h"

// aac encoder
#include "audio_fdkaac.h"

#include "video.h"
// h264 encoder
#include "video_x264.h"

// push packets to stream
#include <librtmp/rtmp.h>
#include <librtmp/log.h>

// other necessary includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <math.h>

#include <float.h>

#include <sys/time.h>

// maximum size of a tag is 11 byte header, 0xFFFFFF payload, 4 byte size
#define MAX_TAG_SIZE 11 + 16777215 + 4

/* ************************************************************************ */
// opaque ptr
//  roughly organized like rtmpcast_param_t
struct rtmpcast_t
{
	struct {
		// rtmp stuff
		RTMP * rtmp;
		int fd;

		// local copy
		FILE * flv;

		uint8_t * tag;

		double start;
	} rtmp;

	struct {
		unsigned int width, height;
		unsigned int framerate;
		unsigned int bitrate;

		struct encoder_video * encoder;

		double timestamp_next;
		double timestamp_increment;
	} video;

	struct {
		unsigned int samplerate;
		unsigned int channels;
		unsigned int bitrate;

		struct encoder_audio * encoder;

		double timestamp_next;
		double timestamp_increment;
	} audio;
};

/* ************************************************************************ */
// helper functions
// get "now" in seconds
static double getTimestamp() {
	struct timeval timecheck;
	gettimeofday(&timecheck, NULL);
	return timecheck.tv_sec + timecheck.tv_usec / 1000000.;
}

// write big-endian values to memory area
static uint8_t * u16be(uint8_t * const p, const uint16_t value) {
	*p = value >> 8 & 0xFF;
	*(p + 1) = value & 0xFF;
	return p + 2;
}
static uint8_t * u24be(uint8_t * const p, const uint32_t value) {
	*p = value >> 16 & 0xFF;
	*(p + 1) = value >> 8 & 0xFF;
	*(p + 2) = value & 0xFF;
	return p + 3;
}
static uint8_t * u32be(uint8_t * const p, const uint32_t value) {
	*p = value >> 24 & 0xFF;
	*(p + 1) = value >> 16 & 0xFF;
	*(p + 2) = value >> 8 & 0xFF;
	*(p + 3) = value & 0xFF;
	return p + 4;
}

// deconstruct a host-native double, repack it as big-endian IEEE 754 double
//  TODO: rather than the test patterns, use actual float.h funcs
static uint8_t * f64be(uint8_t * const p, const double input) {
	const uint8_t * const value = (uint8_t *)&input;
	static const double testVal = 1;
	static const uint8_t testBE[8] = { 0x3F, 0xF0 };
	static const uint8_t testLE[8] = { 0, 0, 0, 0, 0, 0, 0xF0, 0x3F };

	if (memcmp(&testVal, testBE, 8) == 0) {
		// already in big-endian
		memcpy(p, value, 8);
	} else { // if (memcmp(&testVal, testLE, 8) == 0)
		// byteswap
		for (int i = 0; i < 8; i++)
			p[i] = value[7 - i];
	}

	return p + 8;
}

// "pascal" string (uint16 strlen, string content)
static uint8_t * pstring(uint8_t * p, const char * const str) {
	uint16_t string_length = strlen(str);
	p = u16be(p, string_length);
	memcpy(p, str, string_length);
	return p + string_length;
}

// AMF (Action Message Format) serializers
static uint8_t * amf_number(uint8_t * const p, const double value) {
	*p = 0x00;
	return f64be(p + 1, value);
}
static uint8_t * amf_boolean(uint8_t * const p, const uint8_t value) {
	*p = 0x01;
	*(p+1) = (value ? 1 : 0);
	return p + 2;
}
static uint8_t * amf_string(uint8_t * const p, const char * const str) {
	*p = 0x02;
	return pstring(p + 1, str);
}
static uint8_t * amf_ecma_array(uint8_t * const p, const uint32_t entries) {
	*p = 0x08;
	return u32be(p + 1, entries);
}
static uint8_t * amf_ecma_array_end(uint8_t * const p) {
	return u24be(p, 0x000009);
}
static uint8_t * amf_ecma_array_entry(uint8_t * const p, const char * const str, const double value) {
	return amf_number(pstring(p, str), value);
}

/* ************************************************************************ */
// sets up the first 11 bytes of a tag
//  returns pointer to the start of payload area
static uint8_t * flv_TagHeader(uint8_t * p, const uint8_t type, const uint32_t timestamp)
{
	*p = type; p ++; // message type
	// tag[1 - 3] are the message size, which we don't know yet
	p += 3;

	// FLV timestamp is written in an odd format
	p = u24be(p, timestamp & 0x00FFFFFF);
	*p = timestamp >> 24 & 0xFF; p ++;

	return u24be(p, 0); // stream ID
}

// Finishes a tag (corrects Payload Size in bytes 1-3, and appends Tag Size)
//  Returns complete tag size, ready for writing
static uint32_t flv_TagFinish(uint8_t * tag, uint8_t * p)
{
	uint32_t payloadSize = p - (tag + 11);
	u24be(tag + 1, payloadSize);
	u32be(p, 11 + payloadSize);

	return 11 + payloadSize + 4;
}

// FLV Video Packet (AVC format)
//  Composition Time is 0 for all-I frames, but otherwise should be the time diff. between PTS and DTS
static uint8_t * flv_AVCVideoPacket(uint8_t * const p, const unsigned int keyframe, const uint8_t type, const long composition_time)
{
	if (keyframe)
		*p = 0x17;
	else
		*p = 0x27;

	*(p + 1) = type;
	return u24be(p + 2, composition_time);
}

// Allocate an object and give it a URL to work with
struct rtmpcast_t * rtmpcast_init (const struct rtmpcast_param_t * const p)
{
	// Validate a few things
	if (! p) {
		fputs("librtmpcast: ERROR: rtmpcast_param_t is NULL\n", stderr);
		return NULL;
	}
	if (! p->url) {
		fputs("librtmpcast: ERROR: rtmp.url is NULL\n", stderr);
		return NULL;
	}
	if (! (p->video.enable || p->audio.enable)) {
		fputs("librtmpcast: ERROR: video.enable and audio.enable both false\n", stderr);
		return NULL;
	}

	// allocate a struct
	struct rtmpcast_t * r = malloc(sizeof(struct rtmpcast_t));
	if (! r) {
		perror("librtmpcast: ERROR: malloc() returned NULL");
		return NULL;
	}

	/* *************************************************** */
	// allocate a very large buffer for all packets and operations
	r->rtmp.tag = malloc(MAX_TAG_SIZE);

	// copy the callback param
	if (p->video.enable) {
		// copy all video-related params
		r->video.width = p->video.width;
		r->video.height = p->video.height;
		r->video.framerate = p->video.framerate;
		r->video.bitrate = p->video.bitrate;

		// calculate the timestamp interval
		r->video.timestamp_increment = 1.0 / r->video.framerate;

		// set up the encoder
		//  only x264 supported for now
		r->video.encoder = video_x264_create(
			p->video.width,
			p->video.height,
			p->video.framerate,
			p->video.bitrate,
			p->video.callback,
			r->rtmp.tag + 11 + 5
		);

	} else {
		// clear some values in the struct
		r->video.width = 0;
		r->video.height = 0;
		r->video.framerate = 0;
		r->video.bitrate = 0;

		r->video.timestamp_increment = INFINITY;

		r->video.encoder = NULL;
	}

	/* *************************************************** */
	// Initialize the AAC encoder
	if (p->audio.enable) {
		// copy all params
		r->audio.samplerate = p->audio.samplerate;
		r->audio.channels = p->audio.channels;
		r->audio.bitrate = p->audio.bitrate;

		// calculate the timestamp interval
		r->audio.timestamp_increment = 1024.0 / r->audio.samplerate;

		// set up the encoder
		//  only fdkaac supported for now
		r->audio.encoder = audio_fdkaac_create(
			p->audio.channels,
			p->audio.bitrate * 1024,
			p->audio.samplerate,
			p->audio.callback,
			r->rtmp.tag + 11 + 2
		);
	} else {
		r->audio.samplerate = 0;
		r->audio.channels = 0;
		r->audio.bitrate = 0;

		r->audio.timestamp_increment = INFINITY;

		r->audio.encoder = NULL;
	}

	/* *************************************************** */
	// Increase the log level for all RTMP actions
	RTMP_LogSetLevel(RTMP_LOGINFO);
	RTMP_LogSetOutput(stderr);

	/* *************************************************** */
	// Init RTMP code
	r->rtmp.rtmp = RTMP_Alloc();
	RTMP_Init(r->rtmp.rtmp);

	if (r->rtmp.rtmp == NULL) {
		fputs("Failed to create RTMP object\n", stderr);
		audio_fdkaac_close(r->audio.encoder);
		video_x264_close(r->video.encoder);
		free(r->rtmp.tag);
		return NULL;
	}

	RTMP_SetupURL(r->rtmp.rtmp, p->url);
	RTMP_EnableWrite(r->rtmp.rtmp);

	// if user added a filename, open it and prep for writing
	//  if this fails it is non-fatal
	if (p->filename != NULL)
	{
		r->rtmp.flv = fopen(p->filename, "wb");
		if (r->rtmp.flv == NULL) {
			fprintf(stderr, "Failed to open '%s' for writing. Local file output will be disabled.\n", p->filename);
			perror("Error reported was");
			r->rtmp.flv = NULL;
		} else {
			const uint8_t flvHeader[] = { 0x46, 0x4C, 0x56, 0x01, (p->audio.enable ? 0x04 : 0) | (p->video.enable ? 0x01 : 0), 0, 0, 0, 9, 0, 0, 0, 0 };
			fwrite(flvHeader, 1, 13, r->rtmp.flv);
		}
	} else {
		r->rtmp.flv = NULL;
	}

	return r;
}

// Make connection to configured RTMP service, send initial metadata packets
int rtmpcast_connect (struct rtmpcast_t * const r)
{
	// Make RTMP connection to server
	if (! RTMP_Connect(r->rtmp.rtmp, NULL)) {
		fputs("Failed to connect to remote RTMP server\n", stderr);
		return 0;
	}

	// Connect to RTMP stream
	if (! RTMP_ConnectStream(r->rtmp.rtmp, 0)) {
		fputs("Failed to connect to RTMP stream\n", stderr);
		return 0;
	}

	// track the fd for rtmp
	r->rtmp.fd = RTMP_Socket(r->rtmp.rtmp);

	// READY to send the first packet!
	// First event is the onMetaData, which uses AMF (Action Meta Format)
	//  to serialize basic stream params
	uint8_t * p = flv_TagHeader(r->rtmp.tag, 18, 0);

	// script data type is "onMetaData"
	p = amf_string(p, "onMetaData");
	// associative array with various stream parameters
	p = amf_ecma_array(p, 8);
	p = amf_ecma_array_entry(p, "width", r->video.width);
	p = amf_ecma_array_entry(p, "height", r->video.height);
	p = amf_ecma_array_entry(p, "framerate", r->video.framerate);
	p = amf_ecma_array_entry(p, "videocodecid", 7);
	p = amf_ecma_array_entry(p, "videodatarate", r->video.bitrate);
	p = amf_ecma_array_entry(p, "audiocodecid", 10);
	p = amf_ecma_array_entry(p, "audiodatarate", r->audio.bitrate);
	p = amf_ecma_array_entry(p, "audiosamplerate", r->audio.samplerate);
	//p = amf_ecma_array_entry(p, "audiosamplesize", 16);
	p = amf_boolean(pstring(p, "stereo"), r->audio.channels == 2);
	// finalize the array
	p = amf_ecma_array_end(p);

	// calculate tag size and write it
	uint32_t tagSize = flv_TagFinish(r->rtmp.tag, p);

	if (RTMP_Write(r->rtmp.rtmp, (const char *)r->rtmp.tag, tagSize) <= 0) {
		fputs("Failed to RTMP_Write\n", stderr);
		return 0;
	}
	if (r->rtmp.flv) fwrite(r->rtmp.tag, 1, tagSize, r->rtmp.flv);

	// ready to write the tag
	// First event is the onMetaData, which uses AMF (Action Meta Format)
	//  to serialize basic stream params
	p = flv_TagHeader(r->rtmp.tag, 9, 0);

	// Set up an AVC Video Packet (is keyframe, type 0)
	p = flv_AVCVideoPacket(p, 1, 0, 0);

	// video init
	int video_size = video_x264_init(r->video.encoder);
	if (video_size < 0) {
		// error occurred
		fputs("Failed to fdkaac_init\n", stderr);
		return 0;
	}
	p += video_size;

	// calculate tag size and write it
	tagSize = flv_TagFinish(r->rtmp.tag, p);

	if (RTMP_Write(r->rtmp.rtmp, (const char *)r->rtmp.tag, tagSize) <= 0) {
		fputs("Failed to RTMP_Write\n", stderr);
		return 0;
	}
	if (r->rtmp.flv) fwrite(r->rtmp.tag, 1, tagSize, r->rtmp.flv);

	/* ************************************************************************** */
	// NOW!!! we have set up the video encoder.
	//  so let's do audio next - the Initial Audio Packet.
	p = flv_TagHeader(r->rtmp.tag, 8, 0);
	// 0xA0 for "AAC"
	// 0x0F for flags (44khz, stereo, 16bit)
	*p = 0xAF; p++;
	*p = 0; p++;

	int audio_size = audio_fdkaac_init(r->audio.encoder);
	if (audio_size < 0) {
		// error occurred
		fputs("Failed to fdkaac_init\n", stderr);
		return 0;
	}
	p += audio_size;
	// calculate tag size and write it
	tagSize = flv_TagFinish(r->rtmp.tag, p);

	if (RTMP_Write(r->rtmp.rtmp, (const char *)r->rtmp.tag, tagSize) <= 0) {
		fputs("Failed to RTMP_Write\n", stderr);
		return 0;
	}
	if (r->rtmp.flv) fwrite(r->rtmp.tag, 1, tagSize, r->rtmp.flv);

	// Starting timestamp of our video
	r->rtmp.start = getTimestamp();

	r->video.timestamp_next = r->rtmp.start + r->video.timestamp_increment;
	r->audio.timestamp_next = r->rtmp.start + r->audio.timestamp_increment;

	return 1;
}

// Call this periodically to keep the stream flowing
double rtmpcast_update (struct rtmpcast_t * r)
{
	// get the current time
	double now = getTimestamp();

	// find out if we need to emit any frames

	while (now >= r->video.timestamp_next ||
		now >= r->audio.timestamp_next)
	{
		//printf("now = %lf, vid_next = %lf, aud_next = %lf\n", now, r->video.timestamp_next, r->audio.timestamp_next);
		// prioritize the most recent timestamp
		if (r->video.timestamp_next < r->audio.timestamp_next) {
			if (r->video.encoder) {
				// Post our video frame
				uint8_t * p = flv_TagHeader(r->rtmp.tag, 9, 1000 * (r->video.timestamp_next - r->rtmp.start));

				// call out to the chosen encoder
				struct video_return_t v = video_x264_update(r->video.encoder);

				if (v.size < 0) {
					// error in encoding
					fputs("Error when encoding video\n", stderr);
					return 0;
				} else if (v.size > 0) {
					// the encoder did something, need to package it up and ship
					p = flv_AVCVideoPacket(p, v.keyframe, 1, 0);

					// skip the encoder-written tag
					p += v.size;

					// calculate tag size and write it
					uint32_t tagSize = flv_TagFinish(r->rtmp.tag, p);

					//  cast to char* avoids a warning
					if (RTMP_Write(r->rtmp.rtmp, (const char *)r->rtmp.tag, tagSize) <= 0) {
						fputs("Failed to RTMP_Write a frame\n", stderr);
					}
					if (r->rtmp.flv) fwrite(r->rtmp.tag, 1, tagSize, r->rtmp.flv);
				}
			}
			r->video.timestamp_next += r->video.timestamp_increment;
		} else {
			// time for an audio
			if (r->audio.encoder) {
				// build tag header for audio
				uint8_t * p = flv_TagHeader(r->rtmp.tag, 8, 1000 * (r->audio.timestamp_next - r->rtmp.start));
				*p = 0xAF; p++;
				*p = 1; p++;

				// call out to the chosen encoder
				int audio_size = audio_fdkaac_update(r->audio.encoder);
				if (audio_size < 0) {
					// error in encoding
					fputs("Error when encoding audio\n", stderr);
					return 0;
				}
				p += audio_size;

				// calculate tag size and write it
				uint32_t tagSize = flv_TagFinish(r->rtmp.tag, p);

				//  cast to char* avoids a warning
				if (RTMP_Write(r->rtmp.rtmp, (const char *)r->rtmp.tag, tagSize) <= 0) {
					fputs("Failed to RTMP_Write audio block\n", stderr);
				}
				if (r->rtmp.flv) fwrite(r->rtmp.tag, 1, tagSize, r->rtmp.flv);
			}
			r->audio.timestamp_next += r->audio.timestamp_increment;
		}

		// advance now-time
		now = getTimestamp();
	}
	//printf(" -> now = %lf, vid_next = %lf, aud_next = %lf\n", now, r->video.timestamp_next, r->audio.timestamp_next);

	// Handle any packets from the remote to us.
	//  We will use select() to see if packet is waiting,
	//  then read it and dispatch to the handler.
	fd_set set;
	FD_ZERO(&set);
	FD_SET(r->rtmp.fd, &set);

	struct timeval tv = {0, 0};
	if (select(r->rtmp.fd + 1, &set, NULL, NULL, &tv) == -1) {
		perror("Error calling select()");
	}

	// socket is present in read-ready set, safe to call RTMP_ReadPacket
	if (FD_ISSET(r->rtmp.fd, &set)) {
		RTMPPacket packet = { 0 };

		if (RTMP_ReadPacket(r->rtmp.rtmp, &packet) && RTMPPacket_IsReady(&packet)) {
			// this function does all the internal stuff we need
			RTMP_ClientPacket(r->rtmp.rtmp, &packet);
			RTMPPacket_Free(&packet);
		}
	}

	// the time to sleep is the duration between target framestamp and now
	return fmax(0, fmin(r->video.timestamp_next, r->audio.timestamp_next) - now);
}

// Destroy a stream object / free it
void rtmpcast_close (struct rtmpcast_t * r)
{
	/* Flush delayed frames for a clean shutdown */
	// send the end-of-stream indicator
	uint8_t * p = flv_TagHeader(r->rtmp.tag, 9, 1000 * (r->video.timestamp_next - r->rtmp.start));
	// write the empty-body "stream end" tag
	p = flv_AVCVideoPacket(p, 1, 2, 0);
	// calculate tag size and write it
	uint32_t tagSize = flv_TagFinish(r->rtmp.tag, p);

	//  cast to char* avoids a warning
	if (RTMP_Write(r->rtmp.rtmp, (const char *)r->rtmp.tag, tagSize) <= 0) {
		fputs("Failed to RTMP_Write\n", stderr);
	}
	if (r->rtmp.flv) fwrite(r->rtmp.tag, 1, tagSize, r->rtmp.flv);

	/* *************************************************** */
	// CLEANUP CODE
	// Shut down
	if (r->rtmp.flv) fclose(r->rtmp.flv);
	RTMP_Free(r->rtmp.rtmp);
	audio_fdkaac_close(r->audio.encoder);
	video_x264_close(r->video.encoder);
	free(r->rtmp.tag);
}
