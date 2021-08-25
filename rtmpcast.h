#ifndef RTMPCAST_H_
#define RTMPCAST_H_

#include <stdint.h>

// opaque ptr to the encoder / streamer object
struct rtmpcast_t;

// struct containing parameters for the connection
struct rtmpcast_param_t
{
	struct {
		char * url;
	} rtmp;

	struct {
		int enable;

		int (* callback)(uint8_t * frame[3]);

		unsigned int width, height;
		unsigned int framerate;
		unsigned int bitrate;
	} video;

	struct {
		int enable;

		int (* callback)(int16_t * buffer);

		unsigned int samplerate;
		unsigned int channels;
		unsigned int bitrate;
	} audio;
};

// Allocate an object and give it a URL to work with
//  also pass the callbacks
struct rtmpcast_t * rtmpcast_init (const struct rtmpcast_param_t * param);

// Make the first connection / send initial packets
int rtmpcast_connect (struct rtmpcast_t * rtmpcast);
// Call this periodically to keep the stream flowing
//  Returns number of microseconds until the next update is expected
//  A negative number indicates a stream error
double rtmpcast_update (struct rtmpcast_t * rtmpcast);

// Destroy a stream object / free it
void rtmpcast_close (struct rtmpcast_t * rtmpcast);

#endif
