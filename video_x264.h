#ifndef RTMPCAST_VIDEO_X264_H
#define RTMPCAST_VIDEO_X264_H

#include "video.h"

struct encoder_video;

struct encoder_video * video_x264_create(const unsigned int width, const unsigned int height, const unsigned int framerate, const unsigned int bitrate, int (* callback)(unsigned char ** frame), unsigned char * destination);
int video_x264_init(const struct encoder_video * video);
struct video_return_t video_x264_update(const struct encoder_video * video);
void video_x264_close(struct encoder_video * video);

#endif

