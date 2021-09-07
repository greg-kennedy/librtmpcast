#include "audio_fdkaac.h"

// libfdk's AAC encoder header
#include <fdk-aac/aacenc_lib.h>

// for malloc
#include <stdlib.h>
// for memcpy
#include <string.h>
// for fprintf
#include <stdio.h>

// some constants
static int in_buffer_element_sizes[] = { sizeof(INT_PCM) };
static int in_buffer_identifiers[]   = { IN_AUDIO_DATA };
static int out_buffer_element_sizes[] = { sizeof(unsigned char) };
static int out_buffer_identifiers[]   = { OUT_BITSTREAM_DATA };

// structure definition for the encoder (private data)
struct encoder_audio {
	// user callback to generate more audio
	int (* callback)(short int * buffer);

	HANDLE_AACENCODER encoder;
	AACENC_InfoStruct encoder_info;
	AACENC_BufDesc in_buf, out_buf;

	INT_PCM * in_buffers[1];
	int in_buffer_sizes[1];

	unsigned char * out_buffers[1];
	int out_buffer_sizes[1];
};

// Helper function: print a friendly AACENC_ERROR code as a message.
static int print_aacenc_error(const char * const message, const AACENC_ERROR error_code)
{
	const char * error_name, * error_description;

	switch (error_code) {
	case AACENC_OK:
		error_name = "AACENC_OK";
		error_description = "No error happened. All fine.";
		break;

	case AACENC_INVALID_HANDLE:
		error_name = "AACENC_INVALID_HANDLE";
		error_description = "Handle passed to function call was invalid.";
		break;

	case AACENC_MEMORY_ERROR:
		error_name = "AACENC_MEMORY_ERROR";
		error_description = "Memory allocation failed.";
		break;

	case AACENC_UNSUPPORTED_PARAMETER:
		error_name = "AACENC_UNSUPPORTED_PARAMETER";
		error_description = "Parameter not available.";
		break;

	case AACENC_INVALID_CONFIG:
		error_name = "AACENC_INVALID_CONFIG";
		error_description = "Configuration not provided.";
		break;

	case AACENC_INIT_ERROR:
		error_name = "AACENC_INIT_ERROR";
		error_description = "General initialization error.";
		break;

	case AACENC_INIT_AAC_ERROR:
		error_name = "AACENC_INIT_AAC_ERROR";
		error_description = "AAC library initialization error.";
		break;

	case AACENC_INIT_SBR_ERROR:
		error_name = "AACENC_INIT_SBR_ERROR";
		error_description = "SBR library initialization error.";
		break;

	case AACENC_INIT_TP_ERROR:
		error_name = "AACENC_INIT_TP_ERROR";
		error_description = "Transport library initialization error.";
		break;

	case AACENC_INIT_META_ERROR:
		error_name = "AACENC_INIT_META_ERROR";
		error_description = "Meta data library initialization error.";
		break;

	case AACENC_INIT_MPS_ERROR:
		error_name = "AACENC_INIT_MPS_ERROR";
		error_description = "MPS library initialization error.";
		break;

	case AACENC_ENCODE_ERROR:
		error_name = "AACENC_ENCODE_ERROR";
		error_description = "The encoding process was interrupted by an unexpected error.";
		break;

	case AACENC_ENCODE_EOF:
		error_name = "AACENC_ENCODE_EOF";
		error_description = "End of file reached.";
		break;

	default:
		error_name = "AACENC_UNKNOWN";
		error_description = "Unknown error.";
	}

	return fprintf(stderr, "%s: %s (0x%04x): %s\n", message, error_name, error_code, error_description);
}

struct encoder_audio * audio_fdkaac_create(
	const unsigned int channels,
	const unsigned int bitrate,
	const unsigned int samplerate,
	int (* callback)(INT_PCM * buffer),
	unsigned char * const destination)
{
	// create a structure
	struct encoder_audio * e = malloc(sizeof(struct encoder_audio));

	if (e == NULL) {
		perror("librtmpcast: ERROR: audio_fdkaac::audio_fdkaac_create: malloc() returned NULL");
		return NULL;
	}

	// store the callback
	e->callback = callback;

	// get encoder with support for only basic (AAC-LC) and 1 or 2 channels
	AACENC_ERROR err = aacEncOpen(&e->encoder, 0x01, channels);

	if (err != AACENC_OK) {
		print_aacenc_error("Failed calling aacEncOpen", err);
		free(e);
		return NULL;
	}

	// Set all parameters
	//  This macro wraps error printout / handling for us
	#define aacSetParam(x, y) err = aacEncoder_SetParam(e->encoder, x, y); \
		if (err != AACENC_OK) { \
		print_aacenc_error("Failed calling aacSetParam(" #x ", " #y ")", err); \
			aacEncClose(&e->encoder); \
			free(e); \
			return NULL; \
		}

	// give me just AAC-LC output (no HC, SSR, SBR etc)
	aacSetParam(AACENC_AOT, AOT_AAC_LC); // AAC-LC
	// Controls the output format of blocks coming from the encoder
	//  this is just raw outputs (no special framing / container)
	aacSetParam(AACENC_TRANSMUX, TT_MP4_RAW);
	// Better quality at the expense of processing power
	//aacSetParam(AACENC_AFTERBURNER,1);
	aacSetParam(AACENC_BITRATE, bitrate);
	aacSetParam(AACENC_SAMPLERATE, samplerate);
	// channel arrangement
	aacSetParam(AACENC_CHANNELMODE, (channels == 2 ? MODE_2 : MODE_1));
	aacSetParam(AACENC_CHANNELORDER, 1);

	// This strange call is needed to "lock in" the settings for encoding
	err = aacEncEncode(e->encoder, NULL, NULL, NULL, NULL);

	if (err != AACENC_OK) {
		print_aacenc_error("Failed calling initial aacEncEncode", err);
		aacEncClose(&e->encoder);
		free(e);
		return NULL;
	}

	// Now we have encoder info in a struct and can use it for writing audio packets
	err = aacEncInfo(e->encoder, &e->encoder_info);

	if (err != AACENC_OK) {
		print_aacenc_error("Failed to copy encoder info", err);
		aacEncClose(&e->encoder);
		free(e);
		return NULL;
	}

	// ////////
	// alloc the INPUT buffer
	e->in_buf.numBufs           = 1;
	e->in_buffer_sizes[0]       = 1024 * channels * sizeof(INT_PCM);
	e->in_buf.bufSizes          = e->in_buffer_sizes;
	e->in_buffers[0]            = malloc(e->in_buffer_sizes[0]);

	if (e->in_buffers[0] == NULL) {
		perror("librtmpcast: ERROR: audio_fdkaac::audio_fdkaac_create: malloc() returned NULL");
		aacEncClose(&e->encoder);
		free(e);
		return NULL;
	}

	e->in_buf.bufs              = e->in_buffers;
	// use the constants
	e->in_buf.bufferIdentifiers = in_buffer_identifiers;
	e->in_buf.bufElSizes        = in_buffer_element_sizes;

	// ////////
	// set up the OUTPUT buffer
	e->out_buf.numBufs           = 1;
	e->out_buffer_sizes[0]       = 768 * channels * sizeof(unsigned char);
	e->out_buf.bufSizes          = e->out_buffer_sizes;

	e->out_buffers[0]            = destination;
	e->out_buf.bufs              = e->out_buffers;
	// use the constants
	e->out_buf.bufferIdentifiers = out_buffer_identifiers;
	e->out_buf.bufElSizes        = out_buffer_element_sizes;

	return e;
}

// Copy the encoder info into the provided buffer, and return the length of copied bytes
int audio_fdkaac_init(const struct encoder_audio * const e)
{
	memcpy(e->out_buffers[0], e->encoder_info.confBuf, e->encoder_info.confSize);
	return e->encoder_info.confSize;
}

// Encode a block and put it into the provided buffer.  Return bytes copied.
int audio_fdkaac_update(const struct encoder_audio * const e)
{
	AACENC_ERROR err;
	/* *************************************************** */
	// CALL THE AUDIO CALLBACK
	AACENC_InArgs in_args;
	in_args.numAncBytes = 0;
	int callback_ret = e->callback(e->in_buffers[0]);

	// User indicated error, return
	if (callback_ret < 0)
		return callback_ret;

	in_args.numInSamples = callback_ret;

	// Perform the encode
	//  Set output buffer to be start of tag
	AACENC_OutArgs out_args; // does not need init - is set by encode
	err = aacEncEncode(e->encoder, &e->in_buf, &e->out_buf, &in_args, &out_args);

	if (err != AACENC_OK) {
		print_aacenc_error("Failed to encode audio", err);
		return -1;
	}

	// return number of bytes added to the tag.
	return out_args.numOutBytes;
}

// shut everything down
void audio_fdkaac_close(struct encoder_audio * const e)
{
	free(e->in_buffers[0]);
	aacEncClose(&e->encoder);
	free(e);
}
