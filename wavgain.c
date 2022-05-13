/* wavgain.c
** Use to raise gain in a .wav file.
** Usage: wavgain inputfile.wav outputfile.wav gain 
**
** Based on tinywavinfo.c, https://github.com/tinyalsa/tinyalsa/blob/master/utils/tinywavinfo.c
** Copyright 2015, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

struct riff_wave_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t wave_id;
};

struct chunk_header {
    uint32_t id;
    uint32_t sz;
};

struct chunk_fmt {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

static int close = 0;

void stream_close(int sig)
{
    /* allow the stream to be closed gracefully */
    signal(sig, SIG_IGN);
    close = 1;
}

size_t xfread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t sz = fread(ptr, size, nmemb, stream);

    if (sz != nmemb && ferror(stream)) {
        fprintf(stderr, "Error: fread failed\n");
        exit(1);
    }
    return sz;
}

int main(int argc, char **argv)
{
    FILE *input_file;
    FILE *output_file;
    struct riff_wave_header riff_wave_header;
    struct chunk_header chunk_header;
    struct chunk_fmt chunk_fmt;
    char *input_filename;
    char *output_filename;
    int more_chunks = 1;
    float gain = 1;

    if (argc < 4) {
        fprintf(stderr, "Usage: %s inputfile.wav outputfile.wav gain \n", argv[0]);
        return 1;
    }

    input_filename = argv[1];
    output_filename = argv[2];
    gain = atof(argv[3]);

    input_file = fopen(input_filename, "rb");
    if (!input_file) {
        fprintf(stderr, "Unable to open input file '%s'\n", input_filename);
        return 1;
    }

    output_file = fopen(output_filename, "w");
    if (!output_file) {
        fprintf(stderr, "Unable to open output file '%s'\n", output_filename);
        return 1;
    }

    xfread(&riff_wave_header, sizeof(riff_wave_header), 1, input_file);
    if ((riff_wave_header.riff_id != ID_RIFF) ||
        (riff_wave_header.wave_id != ID_WAVE)) {
        fprintf(stderr, "Error: '%s' is not a riff/wave file\n", input_filename);
        fclose(input_file);
        return 1;
    }

    do {
        xfread(&chunk_header, sizeof(chunk_header), 1, input_file);

        switch (chunk_header.id) {
        case ID_FMT:
            xfread(&chunk_fmt, sizeof(chunk_fmt), 1, input_file);
            /* If the format header is larger, skip the rest */
            if (chunk_header.sz > sizeof(chunk_fmt))
                fseek(input_file, chunk_header.sz - sizeof(chunk_fmt), SEEK_CUR);
            break;
        case ID_DATA:
            /* Stop looking for chunks */
            more_chunks = 0;
            break;
        default:
            /* Unknown chunk, skip bytes */
            fseek(input_file, chunk_header.sz, SEEK_CUR);
        }
    } while (more_chunks);
    
    void *buffer;
    int size;
    int num_read;
    int frame_size = 1024;
    unsigned int bytes_per_sample = 0;
    float normalization_factor;

    if (chunk_fmt.bits_per_sample == 32) {
        bytes_per_sample = 4;
        normalization_factor = 2147483648;
    } else if (chunk_fmt.bits_per_sample == 16) {
        bytes_per_sample = 2;
        normalization_factor = 32768;
    }

    size = chunk_fmt.num_channels * bytes_per_sample * frame_size;

    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "Unable to allocate %d bytes\n", size);
        free(buffer);
        return 1;
    }

    char headers_buffer[100];
    memset(headers_buffer, 0, sizeof(headers_buffer));

    long int dataposition = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);
    xfread(headers_buffer, dataposition, 1, input_file);
    fwrite(headers_buffer, dataposition, 1, output_file);

    do {
        num_read = xfread(buffer, 1, size, input_file);
        if (num_read > 0) {
            if (2 == bytes_per_sample) {
                short *buffer_ptr = (short *)buffer;
                for (int i = 0; i < num_read / bytes_per_sample; i += chunk_fmt.num_channels) {
                    for (int ch = 0; ch < chunk_fmt.num_channels; ch++) {
                        short sample = (float) (*buffer_ptr++)*gain;
                        fwrite(&sample, 2, 1, output_file);
                    }
                }
            }
        }
    }while (!close && num_read > 0);

    fclose(input_file);
    fclose(output_file);
    
    return 0;
}