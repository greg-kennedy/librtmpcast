/* ***************************************************************************
example.c - librtmpcast usage example
Greg Kennedy 2021

Generates a video test pattern and some audio tones,
  and uses librtmpcast to stream it to a remote RTMP url
*************************************************************************** */

#include <rtmpcast.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

// video output parameters
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 360
#define VIDEO_FRAMERATE 30
#define VIDEO_BITRATE 700

// audio output parameters
#define AUDIO_SAMPLERATE 44100
#define AUDIO_CHANNELS 2
#define AUDIO_BITRATE 128

// GLOBALS
// set to TRUE to write a companion .flv file for debugging
//#define DEBUG 1

// Current frame number
static unsigned int frame_number = 0;
static unsigned int audio_packet_number = 0;
// Flag to indicate whether we should keep playing the movie
//  Set to 0 to close the program
static int running;
// replacement signal handler that sets running to 0 for clean shutdown
static void sig_handler(int signum)
{
	fprintf(stderr, "Received signal %d (%s), exiting.\n", signum, strsignal(signum));
	running = 0;
}

static int callback_video(uint8_t * frame[3])
{
	// frame is expected to be in YUV420 format
	//  frame[0] of Y at WIDTH * HEIGHT
	//  frame[1] of U at WIDTH/2 * HEIGHT/2
	//  frame[2] of V at WIDTH/2 * HEIGHT/2
	// return negative number on failure

	for (int i = 0; i < VIDEO_HEIGHT / 2; i ++) {
		memset(&frame[0][VIDEO_WIDTH * i * 2], frame_number * 2 + i, VIDEO_WIDTH * 2);
		memset(&frame[1][VIDEO_WIDTH / 2 * i], frame_number * 3 + i, VIDEO_WIDTH / 2);
		memset(&frame[2][VIDEO_WIDTH / 2 * i], frame_number * 5 + i, VIDEO_WIDTH / 2);
	}

	frame_number ++;
	return 0;
}

static int callback_audio(int16_t * buffer)
{
	// buffer is expected to be in signed 16-bit format
	//  the length is 1024 entries * number of channels
	//  audio should be interleaved LR LR LR...
	// return the number of samples in the buffer (can be less than 1024)
	//  or a negative number on error

	for (int i = 0; i < 1024; i ++) {
		for (int c = 0; c < AUDIO_CHANNELS; c ++)
			buffer[i * AUDIO_CHANNELS + c] = audio_packet_number * i;
	}

	audio_packet_number = (audio_packet_number + 1) % 200;
	return 1024 * AUDIO_CHANNELS;
}

/* *************************************************** */
int main(int argc, char * argv[])
{
	// verify one parameter passed
	if (argc != 2) {
		printf("librtmpcast example code\nUsage:\n\t%s <URL>\n", argv[0]);
		return EXIT_SUCCESS;
	}

	/* *************************************************** */
	// Set up parameters to create the librtmpcast object
	struct rtmpcast_param_t param = {0};

	param.url = argv[1];
#if DEBUG
	param.filename = "example.flv";
#endif

	param.video.enable = 1;
	param.video.callback = callback_video;
	param.video.width = VIDEO_WIDTH;
	param.video.height = VIDEO_HEIGHT;
	param.video.framerate = VIDEO_FRAMERATE;
	param.video.bitrate = VIDEO_BITRATE;

	param.audio.enable = 1;
	param.audio.callback = callback_audio;
	param.audio.samplerate = AUDIO_SAMPLERATE;
	param.audio.channels = AUDIO_CHANNELS;
	param.audio.bitrate = AUDIO_BITRATE;

	// All done setting up params!  Let's create the rtmpcast object.
	struct rtmpcast_t * rtmpcast = rtmpcast_init(&param);

	if (rtmpcast == NULL) {
		// something went wrong during construction
		fputs("Failed to create rtmpcast object.", stderr);
		return EXIT_FAILURE;
	}

	// Make the connection to the streaming service.
	//  This should be called immediately before sending regular frames,
	//  so the connection does not time out.
	if (! rtmpcast_connect(rtmpcast)) {
		// connection failed
		fputs("Failed to connect with rtmpcast object.", stderr);
		rtmpcast_close(rtmpcast);
		return EXIT_FAILURE;
	}

	// Let's install some signal handlers for a graceful exit
	running = 1;
	// signal(SIGTERM, sig_handler); signal(SIGINT, sig_handler); signal(SIGQUIT, sig_handler); signal(SIGHUP, sig_handler);

	/* *************************************************** */
	// Ready to start throwing frames at the streamer
	while (running) {
		double delay = rtmpcast_update(rtmpcast);

		if (delay < 0) {
			// a negative return value indicates an error
			fputs("Update failed on rtmpcast object.", stderr);
			rtmpcast_close(rtmpcast);
			return EXIT_SUCCESS;
		}

		// sleep for delay ms
		fprintf(stderr, "Sleeping for %lf seconds\n", delay);
		usleep(delay * 1000000);
	}

	// User has decided to shut down - or the streamer crashed.
	//  Send final stream messages.
	rtmpcast_close(rtmpcast);
	return EXIT_SUCCESS;
}
