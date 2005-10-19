/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <OSUtils.h>
#include <Sound.h>
#include <DrawSprocket/DrawSprocket.h>

#include "quakedef.h"
#include "mac.h"
#include "CarbonSndPlayDB.h"
#define SndPlayDoubleBuffer CarbonSndPlayDoubleBuffer
#define SND_COMPLETION snd_completion

int gFrequency;

static pascal void snd_completion(SndChannelPtr pChannel, SndDoubleBufferPtr pDoubleBuffer);

static unsigned char dma[64*1024];
#define SoundBufferSize (1 * 1024)
SndChannelPtr  			gChannel;            // pointer to the sound channel
SndDoubleBufferHeader 	gHeader;             // the double buffer header
static char sRandomBuf[SoundBufferSize];
// disconnect: WTF?! it's already defined in qsound.h, but i got errors
typedef struct
{
	qbool		gamealive;
	qbool		soundalive;
	qbool		splitbuffer;
	int			channels;
	int			samples;			// mono samples in buffer
	int			submission_chunk;		// don't mix less than this #
	int			samplepos;			// in mono samples
	int			samplebits;
	int			speed;
	unsigned char	*buffer;
} dma_t;

extern volatile dma_t 	*shm;
extern volatile dma_t 	sn;
extern int paintedtime;

qbool SNDDMA_Init(void)
{
	OSErr err;
	int i;

	// Just in case
	if (!gFrequency)
	{
		Com_Printf ("WARNING: Couldn't read sound rate preference\n");
		gFrequency = 22050;
	}
	
	for (i = 0; i < sizeof (sRandomBuf); i++)
		sRandomBuf[i] = rand();
	
	shm = &sn;
	shm->splitbuffer = false;

	err = SndNewChannel (&gChannel, sampledSynth, initStereo | initNoInterp | initNoDrop, nil);
	if (err != noErr)
	{
		Com_Printf("Failed to allocate sound channel. OS error %d\n",err);
		return false;
	}

	gHeader.dbhNumChannels     = 2;
	gHeader.dbhSampleSize      = 16;
	gHeader.dbhSampleRate      = (unsigned long)gFrequency << 16;
	gHeader.dbhCompressionID   = notCompressed;
	gHeader.dbhPacketSize      = 0; // compression related
	gHeader.dbhDoubleBack      = SND_COMPLETION;
	if (!gHeader.dbhDoubleBack)
	{
		Com_Printf("Failed to allocate sound channel completion routine.\n");
		return false;
	}

	sn.channels = gHeader.dbhNumChannels;
	sn.samplebits = gHeader.dbhSampleSize;
	sn.samples = sizeof(dma) / (sn.samplebits/8); // mono samples in buffer
	sn.submission_chunk = SoundBufferSize;        // don't mix less than this #
	sn.samplepos = 0;                             // in mono samples
	sn.speed = gFrequency;
	sn.buffer = dma;

	for (i = 0; i < 2; i++)
	{
#undef malloc
#undef free
		gHeader.dbhBufferPtr[i] = malloc(SoundBufferSize + sizeof(SndDoubleBuffer)+0x10);
		if (!gHeader.dbhBufferPtr[i])
		{
			Com_Printf("Failed to allocate sound channel memory.\n");
			return false;
		}
		// ahh, the sounds of silence
		memset(gHeader.dbhBufferPtr[i]->dbSoundData, 0x00, SoundBufferSize);

//		// start with some horrible noise so we can guage whats going on.
//		memcpy(gHeader.dbhBufferPtr[i]->dbSoundData,sRandomBuf,SoundBufferSize);

		gHeader.dbhBufferPtr[i]->dbNumFrames   = SoundBufferSize/(sn.samplebits/8)/sn.channels;
		gHeader.dbhBufferPtr[i]->dbFlags       = dbBufferReady;
		gHeader.dbhBufferPtr[i]->dbUserInfo[0] = 0;
//		gHeader.dbhBufferPtr[i]->dbUserInfo[1] = SetCurrentA5 ();
		
	}


	err = SndPlayDoubleBuffer (gChannel, &gHeader);
	if (err != noErr)
	{
		Com_Printf("Failed to start sound. OS error %d\n",err);
		return false;
	}

	sn.soundalive = true;

	return true;
}

void SNDDMA_Shutdown(void)
{
	int i;
	if (gChannel)
	{
		SCStatus myStatus;
		// Tell them to stop
		gHeader.dbhBufferPtr[0]->dbFlags |= dbLastBuffer;
		gHeader.dbhBufferPtr[1]->dbFlags |= dbLastBuffer;

		// Wait for the sound's end by checking the channel status.}
		do
		{
			SndChannelStatus(gChannel, sizeof(myStatus), &myStatus);
		}
		while (myStatus.scChannelBusy);

		SndDisposeChannel (gChannel, true);
		
	}
	
	for (i = 0; i < 2; i++)
		free (gHeader.dbhBufferPtr[i]);
}

void SNDDMA_Submit(void)
{
	// we're completion routine based....
}

int sDMAOffset;
int SNDDMA_GetDMAPos(void)
{
	return sDMAOffset/(sn.samplebits/8);
}

static pascal void snd_completion(SndChannelPtr channel, SndDoubleBufferPtr buffer)
{
	int sampleOffset = sDMAOffset/(sn.samplebits/8);
	int maxSamples = SoundBufferSize/(sn.samplebits/8);
	int dmaSamples = sizeof(dma)/(sn.samplebits/8);
	if (paintedtime  - sampleOffset > maxSamples ||
		sampleOffset - paintedtime  < dmaSamples-maxSamples)
	{
		memcpy(buffer->dbSoundData,dma+sDMAOffset,SoundBufferSize);
		sDMAOffset += SoundBufferSize;
		if (sDMAOffset >= sizeof(dma))
			sDMAOffset = 0;
	}
	else
		memset(buffer->dbSoundData, 0, SoundBufferSize);
//		memcpy(buffer->dbSoundData,sRandomBuf,SoundBufferSize); // nasty sound

	// ready to go!
	buffer->dbNumFrames = SoundBufferSize / (sn.samplebits / 8) / sn.channels;
	buffer->dbFlags |= dbBufferReady;
}
