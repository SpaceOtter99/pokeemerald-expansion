#include "variant_colours.h"

static inline int ClampI(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline void Rgb555ToRgb888(u16 c, int *r, int *g, int *b)
{
    *r = (c & 0x1F) * 255 / 31;
    *g = ((c >> 5) & 0x1F) * 255 / 31;
    *b = ((c >> 10) & 0x1F) * 255 / 31;
}

static inline u16 Rgb888ToRgb555(int r, int g, int b)
{
    int r5 = (r * 31 + 127) / 255;
    int g5 = (g * 31 + 127) / 255;
    int b5 = (b * 31 + 127) / 255;
    return (u16)((r5 & 31) | ((g5 & 31) << 5) | ((b5 & 31) << 10));
}

static inline int hue2rgb(int p, int q, int t) {
    if (t < 0)   t += 360;
    if (t >= 360) t -= 360;
    if (t < 60)   return p + (q - p) * t / 60;
    if (t < 180)  return q;
    if (t < 240)  return p + (q - p) * (240 - t) / 60;
    return p;
};

// r,g,b: 0..255 -> h:0..359, s:0..100, l:0..100
static void RgbToHsl(int r, int g, int b, int *h, int *s, int *l)
{
    int max = r; if (g > max) max = g; if (b > max) max = b;
    int min = r; if (g < min) min = g; if (b < min) min = b;
    int sum = max + min;
    int delta = max - min;

    *l = (sum * 100 + 255) / 510; // Round to 0..100
    if (max == 0) { *s = 0; *h = 0; return; }

    int denom = (*l <= 50) ? sum : (510 - sum);
    *s = (delta * 100 + denom / 2) / denom; // 0..100

    if (delta == 0) {
        *h = 0;
    } else if (max == r) {
        int num = (g - b) * 60;
        int den = delta;
        int deg = num >= 0 ? (num + den/2) / den : -(( -num + den/2) / den);
        *h = deg;
        if (*h < 0) *h += 360;
    } else if (max == g) {
        int num = (b - r) * 60;
        int den = delta;
        int deg = num >= 0 ? (num + den/2) / den : -(( -num + den/2) / den);
        *h = deg + 120;
    } else {
        int num = (r - g) * 60;
        int den = delta;
        int deg = num >= 0 ? (num + den/2) / den : -(( -num + den/2) / den);
        *h = deg + 240;
    }
    if (*h >= 360) *h -= 360;
    if (*h < 0)    *h += 360;
}

// h:0..359, s:0..100, l:0..100 -> r,g,b:0..255
static void HslToRgb(int h, int s, int l, int *r, int *g, int *b)
{
    int l255 = (l * 255 + 50) / 100;
    int s255 = (s * 255 + 50) / 100;

    if (s255 == 0) { *r = *g = *b = l255; return; }

    int q = (l255 < 128)
          ? (l255 * (255 + s255) + 127) / 255
          : l255 + s255 - (l255 * s255 + 127) / 255;
    int p = 2 * l255 - q;

    *r = hue2rgb(p, q, h + 120);
    *g = hue2rgb(p, q, h);
    *b = hue2rgb(p, q, h - 120);
}

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

static inline u32 mulberry(u32 *rngState) {
  u32 z = *rngState + 0x6D2B79F5;
  z = (z ^ z >> 15) * (1 | z);
  z ^= z + (z ^ z >> 7) * (61 | z);
  *rngState = z ^ z >> 14;
  return *rngState;
}

static inline u32 randRange(u32 *rngState, u32 max) {
  if (max == 0) return 0;
  u32 limit = 0xFFFFFFFFu - (0xFFFFFFFFu % max);
  u32 num;
  do {
    num = mulberry(rngState);
  } while (num > limit);
  return num % max;
}

static inline u32 randChoice(u32 *rngState) {
  return mulberry(rngState) % 2;
}

static void ApplyOneRange(u16 pal16[16], u8 start, u8 len,
                          u8 hueShiftDeg, u8 satAmtPct, u8 lumAmtPct,
                          bool8 svDownOnly, u8 dirH, u8 dirS, u8 dirL)
{
    if (len <= 0) return;

    u8 iStart = ClampI(start, 0, 15);
    u8 iEnd   = ClampI(start + len, 0, 16);

    // Compute signed deltas for S and V
    int sDelta = satAmtPct;
    int vDelta = lumAmtPct;
    if (svDownOnly) {
        sDelta = -2 * sDelta;
        vDelta = -2 * vDelta;
    } else {
        sDelta = dirS ? sDelta : -sDelta;
        vDelta = dirL ? vDelta : -vDelta;
    }

    for (u8 i = iStart; i < iEnd; ++i) {
        int r, g, b, h, s, l;
        Rgb555ToRgb888(pal16[i], &r, &g, &b);
        RgbToHsl(r, g, b, &h, &s, &l);

        h = dirH ? h + hueShiftDeg : h - hueShiftDeg;
        while (h >= 360) h -= 360;
        while (h < 0)    h += 360;

        s = ClampI(s + sDelta, 0, 100);
        l = ClampI(l + vDelta, 0, 100);

        HslToRgb(h, s, l, &r, &g, &b);
        pal16[i] = Rgb888ToRgb555(r, g, b);
    }
}

void ApplyVariantToPaletteBuffer(u32 species, bool8 shiny, u32 originalPID, u16 pal16[16])
{
    const struct SpeciesVariant *l = GetSpeciesVariants(species);
    if (l == NULL)
        return;

    // Roll the PID by a low nibble then mulberry, to spice things up
    const u8 PIDr = originalPID & 0xFu;
    u32 PID = (originalPID << PIDr) | (originalPID >> (32 - PIDr));
    mulberry(&PID);

    static const s16 sHueTable[8] = { 0, 10, 20, 30, 45, 60, 90, 180 };
    static const u8  sSVTable[4]  = { 0, 5, 10, 25 };

    // Range 1
    if (l->pal1_hue_amount || l->pal1_sat_amount || l->pal1_lum_amount) {
        const u8 start = l->pal1_start;
        const u8 len   = l->pal1_length + 1; // stored as (len-1)
        const u8 hmax   = sHueTable[l->pal1_hue_amount & 7];
        const u8 smax  = sSVTable[l->pal1_sat_amount & 3];
        const u8 lmax  = sSVTable[l->pal1_lum_amount & 3];

        const u8 hue = randRange(&PID, hmax);
        const u8 sat = randRange(&PID, smax);
        const u8 lum = randRange(&PID, lmax);

        ApplyOneRange(
          pal16, start, len,
          hue, sat, lum, l->pal1_sv_down_only,
          randChoice(&PID), randChoice(&PID), randChoice(&PID)
        );
    }

    // Range 2
    if (l->pal2_hue_amount || l->pal2_sat_amount || l->pal2_lum_amount) {
        const u8 start = l->pal2_start;
        const u8 len   = l->pal2_length + 1; // stored as (len-1)
        const u8 hmax   = sHueTable[l->pal2_hue_amount & 7];
        const u8 smax  = sSVTable[l->pal2_sat_amount & 3];
        const u8 lmax  = sSVTable[l->pal2_lum_amount & 3];

        const u8 hue = randRange(&PID, hmax);
        const u8 sat = randRange(&PID, smax);
        const u8 lum = randRange(&PID, lmax);

        ApplyOneRange(
          pal16, start, len,
          hue, sat, lum, l->pal2_sv_down_only,
          randChoice(&PID), randChoice(&PID), randChoice(&PID)
        );
    }
}
