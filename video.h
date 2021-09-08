#ifndef RTMPCAST_VIDEO_H
#define RTMPCAST_VIDEO_H

// Common structures shared between rtmpcast lib and the video modules.
struct video_return_t {
	int keyframe;
	int pts;
	int size;
};

#endif
