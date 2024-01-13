#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
#include <ctype.h>
#include <sndfile.h>
#include <math.h>
#include <spandsp.h>

#include </usr/include/spandsp/test_utils.h>

#define BLOCK_LEN           320
#define MAX_TEST_VECTOR_LEN 40000

#define IN_FILE_NAME        "male.wav"
#define OUT_FILE_NAME       "male_g726_16.wav"

int64_t mse=0;
int64_t sumInput=0;
int sampleCnt=0;

void updateSNR(int16_t input, int16_t output){
    sumInput = sumInput + input*input;
    mse = mse + (input-output)*(input-output);
    sampleCnt++;
}

int16_t outdata[MAX_TEST_VECTOR_LEN];
uint8_t adpcmdata[MAX_TEST_VECTOR_LEN];

int16_t itudata[MAX_TEST_VECTOR_LEN];
uint8_t itu_ref[MAX_TEST_VECTOR_LEN];
uint8_t unpacked[MAX_TEST_VECTOR_LEN];
uint8_t xlaw[MAX_TEST_VECTOR_LEN];


#define G726_ENCODING_NONE          9999
#define SF_MAX_HANDLE   32

static int sf_close_at_exit_registered = false;

static SNDFILE *sf_close_at_exit_list[SF_MAX_HANDLE] =
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void sf_close_at_exit(void)
{
    int i;

    for (i = 0;  i < SF_MAX_HANDLE;  i++)
    {
        if (sf_close_at_exit_list[i])
        {
            sf_close(sf_close_at_exit_list[i]);
            sf_close_at_exit_list[i] = NULL;
        }
    }
}


static int sf_record_handle(SNDFILE *handle)
{
    int i;

    for (i = 0;  i < SF_MAX_HANDLE;  i++)
    {
        if (sf_close_at_exit_list[i] == NULL)
            break;
    }
    if (i >= SF_MAX_HANDLE)
        return -1;
    sf_close_at_exit_list[i] = handle;
    if (!sf_close_at_exit_registered)
    {
        atexit(sf_close_at_exit);
        sf_close_at_exit_registered = true;
    }
    return 0;
}

SPAN_DECLARE(SNDFILE *) sf_open_telephony_read(const char *name, int channels)
{
    SNDFILE *handle;
    SF_INFO info;

    memset(&info, 0, sizeof(info));
    if ((handle = sf_open(name, SFM_READ, &info)) == NULL)
    {
        fprintf(stderr, "    Cannot open audio file '%s' for reading\n", name);
        exit(2);
    }
    if (info.samplerate != SAMPLE_RATE)
    {
        printf("    Unexpected sample rate in audio file '%s'\n", name);
        exit(2);
    }
    if (info.channels != channels)
    {
        printf("    Unexpected number of channels in audio file '%s'\n", name);
        exit(2);
    }
    sf_record_handle(handle);
    return handle;
}

SPAN_DECLARE(SNDFILE *) sf_open_telephony_write(const char *name, int channels)
{
    SNDFILE *handle;
    SF_INFO info;

    memset(&info, 0, sizeof(info));
    info.frames = 0;
    info.samplerate = SAMPLE_RATE;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    info.sections = 1;
    info.seekable = 1;

    if ((handle = sf_open(name, SFM_WRITE, &info)) == NULL)
    {
        fprintf(stderr, "    Cannot open audio file '%s' for writing\n", name);
        exit(2);
    }
    sf_record_handle(handle);
    return handle;
}

SPAN_DECLARE(int) sf_close_telephony(SNDFILE *handle)
{
    int res;
    int i;

    if ((res = sf_close(handle)) == 0)
    {
        for (i = 0;  i < SF_MAX_HANDLE;  i++)
        {
            if (sf_close_at_exit_list[i] == handle)
            {
                sf_close_at_exit_list[i] = NULL;
                break;
            }
        }
    }
    return res;
}


int main(int argc, char *argv[])
{
    g726_state_t *enc_state;
    g726_state_t *dec_state;
    int opt;
    bool itutests;
    int bit_rate;
    SNDFILE *inhandle;
    SNDFILE *outhandle;
    int16_t amp[1024];
    int16_t amp_out[1024];
    int frames;
    int adpcm;
    int packing;

    bit_rate = 16000;
    packing = G726_PACKING_NONE;

    if ((inhandle = sf_open_telephony_read(IN_FILE_NAME, 1)) == NULL)
    {
        fprintf(stderr, "    Cannot open audio file '%s'\n", IN_FILE_NAME);
        exit(2);
    }
    if ((outhandle = sf_open_telephony_write(OUT_FILE_NAME, 1)) == NULL)
    {
        fprintf(stderr, "    Cannot create audio file '%s'\n", OUT_FILE_NAME);
        exit(2);
    }

    printf("ADPCM packing is %d\n", packing);
    enc_state = g726_init(NULL, bit_rate, G726_ENCODING_LINEAR, packing);
    dec_state = g726_init(NULL, bit_rate, G726_ENCODING_LINEAR, packing);

    while ((frames = sf_readf_short(inhandle, amp, 159)))
    {
        for(int i=0;i<159;i++){
            printf("%x||", amp[i]);
        }
        printf("\n===============================\n");
        adpcm = g726_encode(enc_state, adpcmdata, amp, frames);
        frames = g726_decode(dec_state, amp_out, adpcmdata, adpcm);
        for(int i=0;i<159;i++){
            printf("%x||", adpcmdata[i]);
        }
        printf("\n===============================\n");
        for(int i=0;i<159;i++){
            printf("%x||", amp_out[i]);
            updateSNR(amp[i], amp_out[i]);
        }
        printf("\n----------------------------------------------------------\n");
        sf_writef_short(outhandle, amp_out, frames);
    }
    if (sf_close_telephony(inhandle))
    {
        printf("    Cannot close audio file '%s'\n", IN_FILE_NAME);
        exit(2);
    }
    if (sf_close_telephony(outhandle))
    {
        printf("    Cannot close audio file '%s'\n", OUT_FILE_NAME);
        exit(2);
    }
    printf("'%s' transcoded to '%s' at %dbps.\n", IN_FILE_NAME, OUT_FILE_NAME, bit_rate);
    float snr = 10*log10f(sumInput/(mse*1.0f));
    printf("SNR = %f\n", snr);
    printf("So luong mau: %d\n", sampleCnt);
    g726_free(enc_state);
    g726_free(dec_state);

    return 0;
}
