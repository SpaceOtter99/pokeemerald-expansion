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