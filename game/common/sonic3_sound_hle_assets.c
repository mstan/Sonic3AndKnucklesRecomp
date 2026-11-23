#include "sonic3_sound_hle_assets.h"
#include "sonic3_sound_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pointer(const uint8_t *data,unsigned at)
{return (uint32_t)data[at]<<24|(uint32_t)data[at+1]<<16|(uint32_t)data[at+2]<<8|data[at+3];}
static uint32_t offset(uint32_t value)
{value&=0xFFFFFEu;return value>=0x400000?value-0x400000:0;}
static uint32_t data_offset(uint32_t value){return value>=0x400000?value-0x400000:0;}
void s3_hle_sound_assets_free(S3HleSoundAssets *assets)
{if(assets){free(assets->data);memset(assets,0,sizeof *assets);}}
int s3_hle_sound_assets(const uint8_t *image,size_t bytes,S3HleSoundAssets *out,char *error,size_t error_bytes)
{
    if(!out || !image || !bytes)return 0;
    /* Reuse the existing verified data decoder and its normalized streams.
     * Its relocation numbers are converted to host offsets here. The first
     * 8 KiB contains only pointer tables, with no driver CPU instructions. */
    const S3SoundLayout layout={8192,0x400000,0x400100,0x400500,0x400900};
    const S3SoundPCMLayout pcm={0x400A00,0x400F00,0x400F80,0xC6,126};
    uint8_t *data=calloc(1,0x400000);if(!data)return 0;
    S3SoundAssetStats stats={0};
    if(!s3_sound_build_sequences(image,bytes,&layout,data,0x400000,&stats,error,error_bytes)){free(data);return 0;}
    /* Qualified rate metadata only, with all waveforms decoded from the
     * read-only input. No Clone program or assembled driver is required. */
    static const uint8_t rates[68]={193,114,91,77,65,193,193,170,136,91,74,151,130,101,151,208,160,136,114,170,136,119,98,130,95,86,124,136,81,81,124,124,136,136,81,81,124,143,81,81,124,136,119,170,105,81,143,98,81,86,65,124,124,124,81,124,124,81,81,81,124,124,72,130,109,101,98,130};
    S3SoundSource source;
    if(!s3_sound_read_source(image,bytes,&source,error,error_bytes)){free(data);return 0;}
    for(unsigned i=0;i<126;i++){data[0xA00+i*10]=0x12;data[0xA00+i*10+1]=i<68?rates[i]:149;}
    if(!source.standalone_sonic3)data[0xA00+50*10+1]=66;
    for(unsigned i=0;i<68;i++)data[0xF00+i]=data[0xF80+i]=(uint8_t)(0x81+i);
    if(!s3_sound_build_pcm(image,bytes,&pcm,data,0x400000,&stats.bytes,error,error_bytes)){free(data);return 0;}
    S3HleSoundAssets result={0};result.data=data;result.bytes=stats.bytes;
    *out=result;s3_hle_sound_assets_index(out);return 1;
}
void s3_hle_sound_assets_index(S3HleSoundAssets *out)
{
    S3HleSoundAssets result=*out;const uint8_t *data=result.data;
    memset(result.samples,0,sizeof result.samples);
    for(unsigned i=0;i<51;i++){
        result.music[i]=offset(pointer(data,i*4));
    }
    for(unsigned i=0;i<256;i++)result.psg_envelopes[i]=data_offset(pointer(data,0x500+i*4));
    for(unsigned i=0;i<169;i++)result.effects[i]=offset(pointer(data,0x100+i*4));
    for(unsigned i=0;i<32;i++)result.modulation[i]=data_offset(pointer(data,0x900+i*4));
    for(unsigned i=0;i<126;i++){
        const uint8_t *descriptor=data+0xA00+i*10;
        uint32_t start=data_offset(pointer(descriptor,2)),end=data_offset(pointer(descriptor,6));
        if(!start || end<start || end>result.bytes)continue;
        result.samples[i].start=start;result.samples[i].bytes=end-start;
        result.samples[i].rate_numerator=descriptor[1]*25100u;result.samples[i].rate_denominator=256;
    }
    result.samples[0xC6-0x81].rate_numerator=3579545;
    result.samples[0xC6-0x81].rate_denominator=245;
    *out=result;
}
