#include "variant_colours.h"

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