/* Host stub of the gsKit dmaKit calls video.c uses; the test implements them. */
#pragma once
#define DMA_CHANNEL_TOIPU 4
int dmaKit_chan_init(unsigned int channel);
int dmaKit_send(unsigned int channel, void *data, unsigned int qwc);
