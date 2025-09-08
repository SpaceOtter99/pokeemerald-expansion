#include "variant_colours.h"

static inline u8 ClampI(u8 x, u8 lo, u8 hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline void Rgb555Unpack(u16 c, u8 *r5, u8 *g5, u8 *b5)
{
    *r5 = (c & 0x1F);
    *g5 = (c >> 5) & 0x1F;
    *b5 = (c >> 10) & 0x1F;
}

static inline u16 Rgb555Pack(u8 r5, u8 g5, u8 b5)
{
    if (r5 < 0) r5 = 0; else if (r5 > 31) r5 = 31;
    if (g5 < 0) g5 = 0; else if (g5 > 31) g5 = 31;
    if (b5 < 0) b5 = 0; else if (b5 > 31) b5 = 31;
    return (u16)((r5 & 31) | ((g5 & 31) << 5) | ((b5 & 31) << 10));
}

static inline u16 hue2rgb(u16 p, u16 q, u16 t) {
    if (t < 0)   t += 360;
    if (t >= 360) t -= 360;
    if (t < 60)   return p + (q - p) * t / 60;
    if (t < 180)  return q;
    if (t < 240)  return p + (q - p) * (240 - t) / 60;
    return p;
};

// r5,g5,b5: 0..31 -> h:0..359, s:0..100, l:0..100
static void Rgb5ToHsl(int r5, int g5, int b5, int *h, int *s, int *l)
{
    int max = r5; if (g5 > max) max = g5; if (b5 > max) max = b5;
    int min = r5; if (g5 < min) min = g5; if (b5 < min) min = b5;
    int sum = max + min;        // 0..62
    int delta = max - min;      // 0..31

    *l = (sum * 100 + 31) / 62; // Round to 0..100

    if (max == 0) { *s = 0; *h = 0; return; } // black

    int denom = (*l <= 50) ? sum : (62 - sum); // may be 0 at pure white
    *s = (denom ? (delta * 100 + denom / 2) / denom : 0); // 0..100

    if (delta == 0) {
        *h = 0; // shade of grey (no hue)
    } else if (max == r5) {
        int num = (g5 - b5) * 60;
        int den = delta;
        int deg = num >= 0 ? (num + den/2) / den : -(( -num + den/2) / den);
        *h = deg;
        if (*h < 0) *h += 360;
    } else if (max == g5) {
        int num = (b5 - r5) * 60;
        int den = delta;
        int deg = num >= 0 ? (num + den/2) / den : -(( -num + den/2) / den);
        *h = deg + 120;
    } else {
        int num = (r5 - g5) * 60;
        int den = delta;
        int deg = num >= 0 ? (num + den/2) / den : -(( -num + den/2) / den);
        *h = deg + 240;
    }
    if (*h >= 360) *h -= 360;
    if (*h < 0)    *h += 360;
}

// h:0..359, s:0..100, l:0..100 -> r5,g5,b5:0..31
static void HslToRgb5(int h, int s, int l, int *r5, int *g5, int *b5)
{
    int l31 = (l * 31 + 50) / 100;
    int s31 = (s * 31 + 50) / 100;

    if (s31 == 0) { *r5 = *g5 = *b5 = l31; return; }

    int q = (l31 < 16)
          ? (l31 * (31 + s31) + 15) / 31
          : l31 + s31 - (l31 * s31 + 15) / 31;
    int p = 2 * l31 - q;

    *r5 = hue2rgb(p, q, h + 120);
    *g5 = hue2rgb(p, q, h);
    *b5 = hue2rgb(p, q, h - 120);
}

/* -------- Variants data -------- */

const struct SpeciesVariant *GetSpeciesVariants(u32 species)
{
    const struct SpeciesVariant *l = &gSpeciesVariants[species];

    // Treat an all-zero entry as "no variant".
    if (l->pal1_length == 0 && l->pal2_length == 0 &&
        l->pal1_hue_amount == 0 && l->pal1_sat_amount == 0 && l->pal1_lum_amount == 0 &&
        l->pal2_hue_amount == 0 && l->pal2_sat_amount == 0 && l->pal2_lum_amount == 0)
    {
        static const struct SpeciesVariant s = DEFAULT_VARIANT;
        return &s;
    }
    return l;
}

/* -------- PID-driven parameter decode -------- */

#define BITS(v, lo, n)  ( (u32)((v) >> (lo)) & ((1u << (n)) - 1u) )

// Scale N-bit uniform [0..(2^bits-1)] to uniform integer in [0..max-1].
static inline u8 ScaleBitsToMax(u32 bitsVal, u8 max, u8 bits)
{
    if (max == 0) return 0;
    return (u8)((bitsVal * (u32)max) >> bits);
}

/* -------- Core palette application -------- */

static void ApplyOneRange(u16 pal16[16], u8 start, u8 len,
                          u8 hueShiftDeg, u8 satAmtPct, u8 lumAmtPct,
                          bool8 svDownOnly, u8 dirH, u8 dirS, u8 dirL)
{
    if (len == 0) return;

    u8 iStart = ClampI(start, 0, 15);
    u8 iEnd   = ClampI(start + len, 0, 16);

    // Compute signed deltas for S and L
    int sDelta = satAmtPct;
    int lDelta = lumAmtPct;
    if (svDownOnly) {
        sDelta = -2 * sDelta;
        lDelta = -2 * lDelta;
    } else {
        sDelta = dirS ? sDelta : -sDelta;
        lDelta = dirL ? lDelta : -lDelta;
    }

    for (u8 i = iStart; i < iEnd; ++i) {
        int r5, g5, b5, h, s, l;
        Rgb555Unpack(pal16[i], &r5, &g5, &b5);
        Rgb5ToHsl(r5, g5, b5, &h, &s, &l);

        h = dirH ? h + hueShiftDeg : h - hueShiftDeg;
        while (h >= 360) h -= 360;
        while (h < 0)    h += 360;

        s = ClampI(s + sDelta, 0, 100);
        l = ClampI(l + lDelta, 0, 100);

        HslToRgb5(h, s, l, &r5, &g5, &b5);
        pal16[i] = Rgb555Pack(r5, g5, b5);
    }
}

void ApplyVariantToPaletteBuffer(u32 species, bool8 shiny, u32 originalPID, u16 pal16[16])
{
    const struct SpeciesVariant *l = GetSpeciesVariants(species);
    if (l == NULL)
        return;

    static const s16 sHueTable[8] = { 0, 10, 20, 30, 45, 60, 90, 180 };
    static const u8  sSVTable[4]  = { 0, 5, 10, 25 };

    // ---- Palette 1 (low 16 bits) ----
    if (l->pal1_hue_amount || l->pal1_sat_amount || l->pal1_lum_amount) {
        const u8 start = l->pal1_start;
        const u8 len   = l->pal1_length + 1; // stored as (len-1)
        const u8 hmax  = sHueTable[l->pal1_hue_amount & 7];
        const u8 smax  = sSVTable[l->pal1_sat_amount & 3];
        const u8 lmax  = sSVTable[l->pal1_lum_amount & 3];

        u8 hue = ScaleBitsToMax(BITS(originalPID, 0, 7),  hmax, 7);
        u8 sat = ScaleBitsToMax(BITS(originalPID, 7, 3),  smax, 3);
        u8 lum = ScaleBitsToMax(BITS(originalPID, 10, 3), lmax, 3);
        u8 dirH = (u8)BITS(originalPID, 13, 1);
        u8 dirS = (u8)BITS(originalPID, 14, 1);
        u8 dirL = (u8)BITS(originalPID, 15, 1);

        ApplyOneRange(
          pal16, start, len,
          hue, sat, lum, l->pal1_sv_down_only,
          dirH, dirS, dirL
        );
    }

    // ---- Palette 2 (high 16 bits) ----
    if (l->pal2_hue_amount || l->pal2_sat_amount || l->pal2_lum_amount) {
        const u8 start = l->pal2_start;
        const u8 len   = l->pal2_length + 1; // stored as (len-1)
        const u8 hmax  = sHueTable[l->pal2_hue_amount & 7];
        const u8 smax  = sSVTable[l->pal2_sat_amount & 3];
        const u8 lmax  = sSVTable[l->pal2_lum_amount & 3];

        // Extract bits: [16..22]=h, [23..25]=s, [26..28]=l, [29]=dirH, [30]=dirS, [31]=dirL
        u8 hue = ScaleBitsToMax(BITS(originalPID, 16, 7), hmax, 7);
        u8 sat = ScaleBitsToMax(BITS(originalPID, 23, 3), smax, 3);
        u8 lum = ScaleBitsToMax(BITS(originalPID, 26, 3), lmax, 3);
        u8 dirH = (u8)BITS(originalPID, 29, 1);
        u8 dirS = (u8)BITS(originalPID, 30, 1);
        u8 dirL = (u8)BITS(originalPID, 31, 1);

        ApplyOneRange(
          pal16, start, len,
          hue, sat, lum, l->pal2_sv_down_only,
          dirH, dirS, dirL
        );
    }
}
