#include "global.h"
#include "constants/species.h"

struct SpeciesVariant {    
    u8 pal1_start:4;        // Start of 1st palette customisation range
    u8 pal1_length:4;       // Length of 1st palette customisation range (1-indexed)
    u8 pal2_start:4;
    u8 pal2_length:4;
    u8 pal1_hue_amount:3;   // Selects hue from array [0, 10, 20, 30, 45, 60, 90, 180]
    u8 pal1_sat_amount:2;   // Selects sat from array [0, 5, 10, 25]
    u8 pal1_lum_amount:2;   // Selects lum from array [0, 5, 10, 25]
    u8 pal1_sv_down_only:1; // Changes from '+/- sat' to '- 2*sat' (same for lum)
    u8 pal2_hue_amount:3;
    u8 pal2_sat_amount:2;
    u8 pal2_lum_amount:2;
    u8 pal2_sv_down_only:1;
};

// return variant data or return NULL if species has no variants.
const struct SpeciesVariant *GetSpeciesVariants(u32 species);

void ApplyVariantToPaletteBuffer(u32 species, bool8 shiny, u32 PID, u16 pal16[16]);

// Species data helpers

#define HUE_INDEX(h) (     \
    ((h)==0   ? 0 :        \
     (h)<=10  ? 1 :        \
     (h)<=20  ? 2 :        \
     (h)<=30  ? 3 :        \
     (h)<=45  ? 4 :        \
     (h)<=60  ? 5 :        \
     (h)<=90  ? 6 :        \
     /*(h)==180*/ 7) )

#define SAT_INDEX(s) (     \
    ((s)==0   ? 0 :        \
     (s)<=5   ? 1 :        \
     (s)<=10  ? 2 :        \
     /*(s)==25*/ 3) )

#define LUM_INDEX(v) (     \
    ((v)==0   ? 0 :        \
     (v)<=5   ? 1 :        \
     (v)<=10  ? 2 :        \
     /*(v)==25*/ 3) )

#define PAL1(s, l)     \
    .pal1_start = (s), \
    .pal1_length = (l) - 1

#define PAL2(s, l)     \
    .pal2_start = (s), \
    .pal2_length = (l) - 1

#define HSL1(h, s, v, f)                 \
    .pal1_hue_amount = HUE_INDEX(h),       \
    .pal1_sat_amount = SAT_INDEX(s),       \
    .pal1_lum_amount = LUM_INDEX(v),       \
    .pal1_sv_down_only = (f)

#define HSL2(h, s, v, f)                 \
    .pal2_hue_amount = HUE_INDEX(h),       \
    .pal2_sat_amount = SAT_INDEX(s),       \
    .pal2_lum_amount = LUM_INDEX(v),       \
    .pal2_sv_down_only = (f)

#define DEFAULT_VARIANT     \
    {                       \
      .pal1_start = 1,      \
      .pal1_length = 15,    \
      .pal1_hue_amount = 1, \
    }                       \


static const struct SpeciesVariant gSpeciesVariants[] = {
  [SPECIES_BULBASAUR] = {
    PAL1(2, 4),
    HSL1(30, 10, 0, FALSE),
    PAL2(11, 4),
    HSL2(0, 5, 10, FALSE),
  },
  [SPECIES_IVYSAUR] = {
    PAL1(5, 4),
    HSL1(30, 10, 0, FALSE),
    PAL2(2, 3),
    HSL2(45, 10, 0, FALSE),
  },
  [SPECIES_VENUSAUR] = {
    PAL1(1, 4),
    HSL1(30, 10, 0, FALSE),
    PAL2(2, 3),
    HSL2(45, 10, 0, FALSE),
  },
  [SPECIES_MAGIKARP] = {
    PAL1(3, 7),
    HSL1(20, 10, 0, FALSE),
  },
  [SPECIES_GYARADOS] = {
    PAL1(3, 7),
    HSL1(30, 10, 0, TRUE),
    PAL2(10, 3),
    HSL2(10, 25, 0, TRUE),
  },
  [SPECIES_DWEBBLE] = {
    PAL1(7, 6),
    HSL1(20, 25, 10, FALSE),
    PAL2(1,6),
    HSL2(20, 10, 5, FALSE),
  },
  [SPECIES_CRUSTLE] = {
    PAL1(7, 6),
    HSL1(20, 25, 10, FALSE),
    PAL2(1,6),
    HSL2(20, 10, 5, FALSE),
  },
  [SPECIES_SMEARGLE] = {
    PAL1(8, 6),
    HSL1(360, 0, 0, FALSE),
    PAL2(1, 6),
    HSL2(10, 5, 5, TRUE),
  },
  [SPECIES_LILLIPUP] = {
    PAL1(1, 2),
    HSL1(10, 25, 10, TRUE),
  },
  [SPECIES_HERDIER] = {
    PAL1(1, 2),
    HSL1(10, 25, 10, TRUE),
    PAL2(6, 2),
    HSL2(60, 0, 0, FALSE),
  },
  [SPECIES_STOUTLAND] = {
    PAL1(1, 2),
    HSL1(10, 25, 10, TRUE),
    PAL2(6, 2),
    HSL2(60, 0, 0, FALSE),
  },
  [SPECIES_ROCKRUFF] = {
    PAL1(1, 3),
    HSL1(10, 25, 10, TRUE),
  },
  [SPECIES_LYCANROC_MIDDAY] = {
    PAL1(7, 3),
    HSL1(10, 25, 10, TRUE),
  },
  [SPECIES_LYCANROC_MIDNIGHT] = {
    PAL1(8, 6),
    HSL1(10, 25, 10, TRUE),
  },
  [SPECIES_LYCANROC_DUSK] = {
    PAL1(7, 3),
    HSL1(10, 25, 10, TRUE),
  }
};