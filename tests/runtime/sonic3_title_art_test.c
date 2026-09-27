/* Format bounds and actual-ROM validation; no ROM art is embedded here. */
#include "sonic3_title_art.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
int main(int argc, char **argv)
{
    uint8_t decoded[8]; size_t used = 0;
    /* One literal followed by a short overlapping backreference and EOF. */
    const uint8_t packed[] = { 0x41, 0, 0x5A, 0xFF, 0, 0, 0 };
    CHECK(kos(packed, sizeof packed, decoded, sizeof decoded, &used) == 3);
    CHECK(decoded[0] == 0x5A && decoded[1] == 0x5A && decoded[2] == 0x5A);
    CHECK(!kos(packed, sizeof packed - 1, decoded, sizeof decoded, &used));
    CHECK(!kos(packed, sizeof packed, decoded, 2, &used));
    /* Four incremental words, two common words, EOF. */
    const uint8_t mapping[] = { 3, 0, 0, 5, 0x20, 7, 0x0D, 0x1F, 0xE0 };
    uint16_t words[6];
    CHECK(enigma(mapping, sizeof mapping, words, 6) == 6);
    CHECK(words[0] == 5 && words[3] == 8 && words[4] == 0x2007 && words[5] == 0x2007);
    CHECK(!enigma(mapping, sizeof mapping, words, 5));
    CHECK(!enigma(mapping, sizeof mapping - 1, words, 6));
    static S3TitleArt art;
    CHECK(!s3_title_art_decode(mapping, sizeof mapping, &art));
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); CHECK(f);
        fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f); CHECK(size > 0);
        uint8_t *rom = malloc((size_t)size); CHECK(rom);
        CHECK(fread(rom, 1, (size_t)size, f) == (size_t)size); fclose(f);
        CHECK(s3_title_art_decode(rom, (size_t)size, &art));
        unsigned pose = 0, word = 0;
        for (unsigned i = 0; i < sizeof art.pose; ++i) { CHECK(art.pose[i] < 16); pose += art.pose[i] != 0; }
        for (unsigned i = 0; i < sizeof art.word; ++i) { CHECK(art.word[i] < 16); word += art.word[i] != 0; }
        CHECK(pose > 12000 && word > 5000);
        CHECK(art.colors[3] == 0xE); /* Knuckles' brightest red. */
        /* The glove includes seven shared solid-white tiles mapped through
         * palette 0. Dropping that palette punched 8x8 holes in the fist. */
        static const unsigned glove[][2] = {
            {24,96}, {16,104}, {8,112}, {16,112}, {16,120}, {24,120}, {32,120}
        };
        for (unsigned i = 0; i < sizeof glove / sizeof glove[0]; ++i)
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x)
                    CHECK(art.pose[(glove[i][1] + y) * S3_TITLE_POSE_W + glove[i][0] + x] == 11);
        /* The adjacent Sonic art and hand placeholders must stay excluded. */
        CHECK(art.pose[40 * S3_TITLE_POSE_W] == 0);
        CHECK(art.pose[64 * S3_TITLE_POSE_W] == 0);
        rom[ARMY_MAP_TITLE_POSE] = 0xFF;
        CHECK(!s3_title_art_decode(rom, (size_t)size, &art));
        free(rom);
    }
    puts("Title art bounds, overlap and mappings passed");
    return 0;
}
