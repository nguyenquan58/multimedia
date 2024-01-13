#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sndfile.h>
#include <math.h>
#include "spandsp.h"
#include </usr/include/spandsp/test_utils.h>

#define BLOCK_LEN           160

#define IN_FILE_NAME        "male_g711.wav"
#define ENCODED_FILE_NAME   "g711.g711"
#define OUT_FILE_NAME       "male_output_g711.wav"

int64_t mse=0;
int64_t sumInput=0;
int sampleCnt=0;

void updateSNR(int16_t input, int16_t output){
    sumInput = sumInput + input*input;
    mse = mse + (input-output)*(input-output);
    sampleCnt++;
}

int16_t amp[65536];
uint8_t ulaw_data[65536];
uint8_t alaw_data[65536];

const uint8_t alaw_1khz_sine[] = {0x34, 0x21, 0x21, 0x34, 0xB4, 0xA1, 0xA1, 0xB4};
const uint8_t ulaw_1khz_sine[] = {0x1E, 0x0B, 0x0B, 0x1E, 0x9E, 0x8B, 0x8B, 0x9E};

static void compliance_tests(int log_audio)
{
    SNDFILE *outhandle;
    power_meter_t power_meter;
    int outframes;
    int i;
    int block;
    int pre;
    int post;
    int post_post;
    int alaw_failures;
    int ulaw_failures;
    float worst_alaw;
    float worst_ulaw;
    float tmp;
    int len;
    g711_state_t *enc_state;
    g711_state_t *transcode;
    g711_state_t *dec_state;

    outhandle = NULL;
    if (log_audio)
    {
        if ((outhandle = sf_open_telephony_write(OUT_FILE_NAME, 1)) == NULL)
        {
            fprintf(stderr, "    Cannot create audio file '%s'\n", OUT_FILE_NAME);
            exit(2);
        }
    }

    printf("Conversion accuracy tests.\n");
    alaw_failures = 0;
    ulaw_failures = 0;
    worst_alaw = 0.0;
    worst_ulaw = 0.0;
    for (block = 0;  block < 1;  block++)
    {
        for (i = 0;  i < 65536;  i++)
        {
            pre = i - 32768;
            post = alaw_to_linear(linear_to_alaw(pre));
            if (abs(pre) > 140)
            {
                tmp = (float) abs(post - pre)/(float) abs(pre);
                if (tmp > 0.10)
                {
                    printf("A-law: Excessive error at %d (%d)\n", pre, post);
                    alaw_failures++;
                }
                if (tmp > worst_alaw)
                    worst_alaw = tmp;
            }
            else
            {
                if (abs(post - pre) > 15)
                {
                    printf("A-law: Excessive error at %d (%d)\n", pre, post);
                    alaw_failures++;
                }
            }
            amp[i] = post;
        }
        if (log_audio)
        {
            outframes = sf_writef_short(outhandle, amp, 65536);
            if (outframes != 65536)
            {
                fprintf(stderr, "    Error writing audio file\n");
                exit(2);
            }
        }
        for (i = 0;  i < 65536;  i++)
        {
            pre = i - 32768;
            post = ulaw_to_linear(linear_to_ulaw(pre));
            if (abs(pre) > 40)
            {
                tmp = (float) abs(post - pre)/(float) abs(pre);
                if (tmp > 0.10)
                {
                    printf("u-law: Excessive error at %d (%d)\n", pre, post);
                    ulaw_failures++;
                }
                if (tmp > worst_ulaw)
                    worst_ulaw = tmp;
            }
            else
            {
                if (abs(post - pre) > 4)
                {
                    printf("u-law: Excessive error at %d (%d)\n", pre, post);
                    ulaw_failures++;
                }
            }
            amp[i] = post;
        }
        if (log_audio)
        {
            outframes = sf_writef_short(outhandle, amp, 65536);
            if (outframes != 65536)
            {
                fprintf(stderr, "    Error writing audio file\n");
                exit(2);
            }
        }
    }
    printf("Worst A-law error (ignoring small values) %f%%\n", worst_alaw*100.0);
    printf("Worst u-law error (ignoring small values) %f%%\n", worst_ulaw*100.0);
    if (alaw_failures  ||  ulaw_failures)
    {
        printf("%d A-law values with excessive error\n", alaw_failures);
        printf("%d u-law values with excessive error\n", ulaw_failures);
        printf("Tests failed\n");
        exit(2);
    }

    printf("Cyclic conversion repeatability tests.\n");
    for (i = 0;  i < 65536;  i++)
    {
        pre = i - 32768;
        post = alaw_to_linear(linear_to_alaw(pre));
        post_post = alaw_to_linear(linear_to_alaw(post));
        if (post_post != post)
        {
            printf("A-law second round trip mismatch - at %d, %d != %d\n", pre, post, post_post);
            printf("Tests failed\n");
            exit(2);
        }
        post = ulaw_to_linear(linear_to_ulaw(pre));
        post_post = ulaw_to_linear(linear_to_ulaw(post));
        if (post_post != post)
        {
            printf("u-law round trip mismatch - at %d, %d != %d\n", pre, post, post_post);
            printf("Tests failed\n");
            exit(2);
        }
    }

    printf("Reference power level tests.\n");
    power_meter_init(&power_meter, 7);

    for (i = 0;  i < 8000;  i++)
    {
        amp[i] = ulaw_to_linear(ulaw_1khz_sine[i & 7]);
        power_meter_update(&power_meter, amp[i]);
    }
    printf("Reference u-law 1kHz tone is %fdBm0\n", power_meter_current_dbm0(&power_meter));
    if (log_audio)
    {
        outframes = sf_writef_short(outhandle, amp, 8000);
        if (outframes != 8000)
        {
            fprintf(stderr, "    Error writing audio file\n");
            exit(2);
        }
    }
    if (0.1f < fabs(power_meter_current_dbm0(&power_meter)))
    {
        printf("Test failed.\n");
        exit(2);
    }

    for (i = 0;  i < 8000;  i++)
    {
        amp[i] = alaw_to_linear(alaw_1khz_sine[i & 7]);
        power_meter_update(&power_meter, amp[i]);
    }
    printf("Reference A-law 1kHz tone is %fdBm0\n", power_meter_current_dbm0(&power_meter));
    if (log_audio)
    {
        outframes = sf_writef_short(outhandle, amp, 8000);
        if (outframes != 8000)
        {
            fprintf(stderr, "    Error writing audio file\n");
            exit(2);
        }
    }
    if (0.1f < fabs(power_meter_current_dbm0(&power_meter)))
    {
        printf("Test failed.\n");
        exit(2);
    }

    printf("Testing transcoding A-law -> u-law -> A-law\n");
    for (i = 0;  i < 256;  i++)
    {
        if (alaw_to_ulaw(ulaw_to_alaw(i)) != i)
        {
            if (abs(alaw_to_ulaw(ulaw_to_alaw(i)) - i) > 1)
            {
                printf("u-law -> A-law -> u-law gave %d -> %d\n", i, alaw_to_ulaw(ulaw_to_alaw(i)));
                printf("Test failed\n");
                exit(2);
            }
        }
    }

    printf("Testing transcoding u-law -> A-law -> u-law\n");
    for (i = 0;  i < 256;  i++)
    {
        if (ulaw_to_alaw(alaw_to_ulaw(i)) != i)
        {
            if (abs(alaw_to_ulaw(ulaw_to_alaw(i)) - i) > 1)
            {
                printf("A-law -> u-law -> A-law gave %d -> %d\n", i, ulaw_to_alaw(alaw_to_ulaw(i)));
                printf("Test failed\n");
                exit(2);
            }
        }
    }

    enc_state = g711_init(NULL, G711_ALAW);
    transcode = g711_init(NULL, G711_ALAW);
    dec_state = g711_init(NULL, G711_ULAW);

    len = 65536;
    for (i = 0;  i < len;  i++)
        amp[i] = i - 32768;
    len = g711_encode(enc_state, alaw_data, amp, len);
    len = g711_transcode(transcode, ulaw_data, alaw_data, len);
    len = g711_decode(dec_state, amp, ulaw_data, len);
    if (len != 65536)
    {
        printf("Block coding gave the wrong length - %d instead of %d\n", len, 65536);
        printf("Test failed\n");
        exit(2);
    }
    for (i = 0;  i < len;  i++)
    {
        pre = i - 32768;
        post = amp[i];
        if (abs(pre) > 140)
        {
            tmp = (float) abs(post - pre)/(float) abs(pre);
            if (tmp > 0.10)
            {
                printf("Block: Excessive error at %d (%d)\n", pre, post);
                exit(2);
            }
        }
        else
        {
            if (abs(post - pre) > 15)
            {
                printf("Block: Excessive error at %d (%d)\n", pre, post);
                exit(2);
            }
        }
    }
    g711_free(enc_state);
    g711_free(transcode);
    g711_free(dec_state);

    if (log_audio)
    {
        if (sf_close_telephony(outhandle))
        {
            fprintf(stderr, "    Cannot close audio file '%s'\n", OUT_FILE_NAME);
            exit(2);
        }
    }

    printf("Tests passed.\n");
}

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
    SNDFILE *inhandle;
    SNDFILE *outhandle;
    int outframes;
    int opt;
    int samples;
    int len2;
    int len3;
    int basic_tests;
    int law;
    int encode;
    int decode;
    int file;
    const char *in_file;
    const char *out_file;
    g711_state_t *enc_state;
    g711_state_t *dec_state;
    int16_t indata[BLOCK_LEN];
    int16_t outdata[BLOCK_LEN];
    uint8_t g711data[BLOCK_LEN];

    basic_tests = false;
    law = G711_ALAW;
    encode = false;
    decode = false;
    in_file = NULL;
    out_file = NULL;

    if (basic_tests)
    {
        compliance_tests(true);
    }
    else
    {
        if (!decode  &&  !encode)
        {
            decode =
            encode = true;
        }
        if (in_file == NULL)
        {
            in_file = (encode)  ?  IN_FILE_NAME  :  ENCODED_FILE_NAME;
        }
        if (out_file == NULL)
        {
            out_file = (decode)  ?  OUT_FILE_NAME  :  ENCODED_FILE_NAME;
        }
        inhandle = NULL;
        outhandle = NULL;
        file = -1;
        enc_state = NULL;
        dec_state = NULL;
        if (encode)
        {
            if ((inhandle = sf_open_telephony_read(in_file, 1)) == NULL)
            {
                fprintf(stderr, "    Cannot open audio file '%s'\n", in_file);
                exit(2);
            }
            enc_state = g711_init(NULL, law);
        }
        else
        {
            if ((file = open(in_file, O_RDONLY)) < 0)
            {
                fprintf(stderr, "    Failed to open '%s'\n", in_file);
                exit(2);
            }
        }
        if (decode)
        {
            if ((outhandle = sf_open_telephony_write(out_file, 1)) == NULL)
            {
                fprintf(stderr, "    Cannot create audio file '%s'\n", out_file);
                exit(2);
            }
            dec_state = g711_init(NULL, law);
        }
        else
        {
            if ((file = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
            {
                fprintf(stderr, "    Failed to open '%s'\n", out_file);
                exit(2);
            }
        }
        for (;;)
        {
            if (encode)
            {
                samples = sf_readf_short(inhandle, indata, BLOCK_LEN);
                if (samples <= 0)
                    break;
                len2 = g711_encode(enc_state, g711data, indata, samples);
                for(int i=0;i<BLOCK_LEN;i++){
                    printf("%x||", indata[i]);
                }
                printf("\n=========================\n");
                for(int i=0;i<BLOCK_LEN;i++){
                    printf("%x||", g711data[i]);
                }
                printf("\n=========================\n");
            }
            else
            {
                len2 = read(file, g711data, BLOCK_LEN);
                if (len2 <= 0)
                    break;
            }
            if (decode)
            {
                len3 = g711_decode(dec_state, outdata, g711data, len2);
                outframes = sf_writef_short(outhandle, outdata, len3);
                if (outframes != len3)
                {
                    fprintf(stderr, "    Error writing audio file\n");
                    exit(2);
                }
                for(int i=0;i<BLOCK_LEN;i++){
                    printf("%x||", outdata[i]);
                    updateSNR(indata[i], outdata[i]);
                }
                printf("\n----------------------------------------------------------------------\n");
            }
            else
            {
                len3 = write(file, g711data, len2);
                if (len3 <= 0)
                    break;
            }
        }
        if (encode)
        {
            if (sf_close_telephony(inhandle))
            {
                fprintf(stderr, "    Cannot close audio file '%s'\n", IN_FILE_NAME);
                exit(2);
            }
        }
        else
        {
            close(file);
        }
        if (decode)
        {
            if (sf_close_telephony(outhandle))
            {
                fprintf(stderr, "    Cannot close audio file '%s'\n", OUT_FILE_NAME);
                exit(2);
            }
        }
        else
        {
            close(file);
        }
        printf("'%s' translated to '%s' using %s.\n", in_file, out_file, (law == G711_ALAW)  ?  "A-law"  :  "u-law");
        float snr = 10*log10f(sumInput/(mse*1.0f));
        printf("SNR = %f\n", snr);
        printf("So luong mau: %d\n", sampleCnt);
    }
    return 0;
}
