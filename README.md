# librtmpcast
C library for streaming to Twitch (or other RTMP-based services)

Greg Kennedy 2021

**THIS IS BETA SOFTWARE.  INTERFACE AND FEATURES SUBJECT TO CHANGE.  USE AT YOUR OWN RISK!**

## Overview
This is a C library which eases the process of streaming audio and video content to RTMP-based services for broadcast.  The target service is Twitch.tv, which accepts only a particular combination of video + audio codecs (h.264 video + AAC audio).  Other services may also work with this library (Periscope, Wowza, Youtube, etc) but are not the primary focus.

RTMP is a streaming media protocol based around FLV (Flash Video), which is in turn a container format that wraps one or more video and audio streams together in interleaved packets.  Getting the specifics of FLV encoding, h.264 settings etc. can be a challenge.  This library's goal is to abstract much of this away, leaving only these steps:

* Create the streamer object and give it a Stream URL, video params (resolution, desired framerate), and audio params (samplerate, AAC bitrate)
* Provide callbacks to serve the next video and/or audio frame as needed
  * Within the callbacks, provide a still image (YUV420 format) or a 1024-sample audio buffer
* Periodically call the librtmpcast polling function with the object, where it will check timestamps, collect additional frames and dispatch to network as needed
* Close streaming object at the end

Using this library it should be possible to use Twitch as an output device for an application, without needing the additional setup of e.g. a graphical environment + screen recording software, and without the large dependency set of FFmpeg or other video processing libraries.  The tradeoff is that librtmpcast lacks the flexibility of these other solutions.  If you can live within the restrictions, perhaps librtmpcast is the right solution for your needs.

## Dependencies
librtmpcast is effectively a real-time muxer and uses other libraries to do the heavy lifting of encoding.  The dependencies are:

* Protocol
  * [librtmp](https://rtmpdump.mplayerhq.hu/)
* Video
  * [libx264](https://www.videolan.org/developers/x264.html)
* Audio
  * [libfdk-aac](https://github.com/mstorsjo/fdk-aac)
