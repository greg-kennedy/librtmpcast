#ifndef AUDIO_FDKAAC_H
#define AUDIO_FDKAAC_H

struct encoder_audio;

struct encoder_audio * audio_fdkaac_create(const unsigned int channels, const unsigned int bitrate, const unsigned int samplerate, int (* callback)(short int * buffer), unsigned char * destination);
int audio_fdkaac_init(const struct encoder_audio * audio);
int audio_fdkaac_update(const struct encoder_audio * audio);
void audio_fdkaac_close(struct encoder_audio * audio);

#endif
