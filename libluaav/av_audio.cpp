
#include "RtAudio.h"
#include "av.hpp"

#include <cstring>
#include <cstdlib>

//#define AV_AUDIO_MSGBUFFER_SIZE_DEFAULT (1024 * 1024)

// the FFI exposed object:
static av_Audio audio;

// the internal object:
static RtAudio rta;

// the audio-thread Lua state:
//static lua_State * AL = 0;

int av_rtaudio_callback(void *outputBuffer, 
						void *inputBuffer, 
						unsigned int frames,
						double streamTime, 
						RtAudioStreamStatus status, 
						void *data) {
	
	audio.input = (float *)inputBuffer;
	audio.output = (float *)outputBuffer;
	audio.frames = frames;
	
	double newtime = audio.time + frames / audio.samplerate;
	size_t size = sizeof(float) * frames;
	
	//printf("%d %d %d %d\n", audio.blockread, audio.blocks, audio.blocksize, size);
	
	// zero outbuffers:
	//memset(outputBuffer, 0, size);
	
	// copy in the buffers:
	float * dst = audio.output;
	float * src = audio.buffer + audio.blockread * audio.blocksize * audio.outchannels;
	memcpy(dst, src, size * audio.outchannels);
	
	/*
		If input goes into the same location, we probably won't get it until much later.
	*/
	dst = audio.inbuffer + ((audio.blockread + audio.block_io_latency) % audio.blocks) * audio.blocksize * audio.outchannels;
	src = audio.input;
	memcpy(dst, src, size * audio.outchannels);
	// advance the read head:
	audio.blockread++;
	if (audio.blockread >= audio.blocks) audio.blockread = 0;
	
	
	// this calls back into Lua via FFI:
	if (audio.onframes) {
		(audio.onframes)(&audio, newtime, audio.input, audio.output, frames);
	}
	
	audio.time = newtime;
	
	return 0;
}

AV_EXPORT void av_audio_start() {
	if (rta.isStreamRunning()) {
		rta.stopStream();
	}
	if (rta.isStreamOpen()) {
		// close it:
		rta.closeStream();
	}	
	
	unsigned int devices = rta.getDeviceCount();
	if (devices < 1) {
		printf("No audio devices found\n");
		return;
	}
	
	RtAudio::DeviceInfo info;
	RtAudio::StreamParameters iParams, oParams;
	
	printf("Available audio devices:\n");
	for (unsigned int i=0; i<devices; i++) {
		info = rta.getDeviceInfo(i);
		printf("Device %d: %dx%d (%d) %s\n", i, info.inputChannels, info.outputChannels, info.duplexChannels, info.name.c_str());
	}
	
	
	info = rta.getDeviceInfo(audio.indevice);
	printf("Using audio input %d: %dx%d (%d) %s\n", audio.indevice, info.inputChannels, info.outputChannels, info.duplexChannels, info.name.c_str());
	
	audio.inchannels = info.inputChannels;
	
	iParams.deviceId = audio.indevice;
	iParams.nChannels = audio.inchannels;
	iParams.firstChannel = 0;
	
	info = rta.getDeviceInfo(audio.outdevice);
	printf("Using audio output %d: %dx%d (%d) %s\n", audio.outdevice, info.inputChannels, info.outputChannels, info.duplexChannels, info.name.c_str());
	
	audio.outchannels = info.outputChannels;
	
	oParams.deviceId = audio.outdevice;
	oParams.nChannels = audio.outchannels;
	oParams.firstChannel = 0;

	RtAudio::StreamOptions options;
	//options.flags |= RTAUDIO_NONINTERLEAVED;
	options.streamName = "av";
	
	try {
	
		{
			int blockspersecond = audio.samplerate / audio.blocksize;
			audio.blocks = blockspersecond + 1;
			audio.blockstep = audio.blocksize * audio.outchannels;
			
			if (audio.buffer) free(audio.buffer);
			int len = audio.blocksize * audio.outchannels * audio.blocks;
			audio.buffer = (float *)calloc(len, sizeof(float));
			
			if (audio.inbuffer) free(audio.inbuffer);
			len = audio.blocksize * audio.inchannels * audio.blocks;
			audio.inbuffer = (float *)calloc(len, sizeof(float));
			
			audio.blockread = 0;
			audio.blockwrite = 0;
			
			audio.block_io_latency = 1 + int(audio.latency_seconds * audio.samplerate / audio.blocksize);
		}
	
		rta.openStream( &oParams, &iParams, RTAUDIO_FLOAT32, audio.samplerate, &audio.blocksize, &av_rtaudio_callback, NULL, &options );
		rta.startStream();
		printf("Audio started\n");
	}
	catch ( RtError& e ) {
		fprintf(stderr, "%s\n", e.getMessage().c_str());
	}
}

AV_EXPORT av_Audio * av_audio_get() {
	static bool initialized = false;
	if (!initialized) {
		initialized = true;
		
		rta.showWarnings( true );		
		
		// defaults:
		audio.samplerate = 44100;
		audio.blocksize = 256;
		audio.inchannels = 2;
		audio.outchannels = 2;
		audio.time = 0;
		audio.latency_seconds = 1;
		audio.indevice = rta.getDefaultInputDevice();
		audio.outdevice = rta.getDefaultOutputDevice();
		//audio.msgbuffer.size = AV_AUDIO_MSGBUFFER_SIZE_DEFAULT;
		//audio.msgbuffer.read = 0;
		//audio.msgbuffer.write = 0;
		//audio.msgbuffer.data = (unsigned char *)malloc(audio.msgbuffer.size);
		
		audio.onframes = 0;
		
		audio.latency_seconds = 0.1;
		audio.block_io_latency = 1 + int(audio.latency_seconds * audio.samplerate / audio.blocksize);
		
		// one second of ringbuffer:
		int blockspersecond = audio.samplerate / audio.blocksize;
		audio.blocks = blockspersecond + 1;
		audio.blockstep = audio.blocksize * audio.outchannels;
		
		int len = audio.blocksize * audio.outchannels * audio.blocks;
		audio.buffer = (float *)calloc(len, sizeof(float));
		
		len = audio.blocksize * audio.inchannels * audio.blocks;
		audio.inbuffer = (float *)calloc(len, sizeof(float));
		
		audio.blockread = 0;
		audio.blockwrite = 0;
		//AL = av_init_lua();
		
		// unique to audio thread:
		/*
		if (luaL_dostring(AL, "require 'audioprocess'")) {
			printf("error: %s\n", lua_tostring(AL, -1));
			initialized = false;
		} 
		*/
	}
	return &audio;
}
