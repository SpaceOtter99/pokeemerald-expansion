#include "variant_colours.h"

/* ---------- helpers ---------- */

static inline u8 ClampU8(u16 x, u8 lo, u8 hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return (u8)x;
}

static inline u8 ClampSignedToU8(s16 x, s16 lo, s16 hi)
{
    if (x < lo) return (u8)lo;
    if (x > hi) return (u8)hi;
    return (u8)x;
}

static inline void Rgb555Unpack(u16 c, u8 *r5, u8 *g5, u8 *b5)
{
    *r5 = (u8)( c & 0x1Fu);
    *g5 = (u8)((c >>  5) & 0x1Fu);
    *b5 = (u8)((c >> 10) & 0x1Fu);
}

static inline u16 Rgb555Pack(u8 r5, u8 g5, u8 b5)
{
    if (r5 > 31) r5 = 31;
    if (g5 > 31) g5 = 31;
    if (b5 > 31) b5 = 31;
    return (u16)((r5 & 31u) | ((u16)(g5 & 31u) << 5) | ((u16)(b5 & 31u) << 10));
}

// p,q in [0..31]; t in [0..255];
static inline u8 hue2rgb(u8 p, u8 q, u8 t)
{
    // degrees from [0..360] mapped to [0..255]
    const u8 T60  = 43;
    const u8 T180 = 128;
    const u8 T240 = 171;

    s16 P = (s16)p, Q = (s16)q;

    if (t < T60)      return (u8)(P + (Q - P) * (s16)t / (s16)T60);
    if (t < T180)     return (u8)Q;
    if (t < T240)     return (u8)(P + (Q - P) * (s16)(T240 - t) / (s16)(T240 - T180)); /* /43 */
    return (u8)P;
}

// r5,g5,b5: 0..31 -> h:0..255, s:0..100, l:0..100
static void Rgb5ToHsl(u8 r5, u8 g5, u8 b5, u8 *h, u8 *s, u8 *l)
{
    u8 max = r5; if (g5 > max) max = g5; if (b5 > max) max = b5;
    u8 min = r5; if (g5 < min) min = g5; if (b5 < min) min = b5;

    u16 sum   = (u16)max + (u16)min; // 0..62
    u8  delta = (u8)(max - min);     // 0..31

    // L: 0..100 (rounded)
    u8 L = (u8)((sum * 100u + 31u) / 62u);
    *l = L;

    if (max == 0) { *s = 0; *h = 0; return; } // black

    u8  denom = (L <= 50) ? (u8)sum : (u8)(62u - sum); // may be 0 at pure white
    u16 S = denom ? ((u16)delta * 100u + denom/2u) / denom : 0; // 0..100
    *s = (u8)S;

    if (delta == 0) {
      *h = 0; // shade of grey
      return;
    }

    s16 num;
    u8  sector_add;
    if (max == r5) {
      num = (s16)g5 - (s16)b5;
      sector_add = 0;
    } else if (max == g5) {
      num = (s16)b5 - (s16)r5;
      sector_add = 85;
    } else /* max == b5 */{
      num = (s16)r5 - (s16)g5;
      sector_add = 171;
    }

    s16 deg = (num >= 0)
        ? ( (num * 43 + (s16)delta/2) / (s16)delta )
        : -( ((-num) * 43 + (s16)delta/2) / (s16)delta );

    *h = (u8)((s16)sector_add + deg);
}

// h:0..255, s:0..100, l:0..100 -> r5,g5,b5:0..31
static void HslToRgb5(u8 h, u8 s, u8 l, u8 *r5, u8 *g5, u8 *b5)
{
    u8 L31 = (u8)((l * 31u + 50u) / 100u);
    u8 S31 = (u8)((s * 31u + 50u) / 100u);

    if (S31 == 0) { *r5 = *g5 = *b5 = L31; return; }

    u8 Q = (L31 < 16)
         ? (u8)((L31 * (31u + S31) + 15u) / 31u)
         : (u8)(L31 + S31 - (u16)(L31 * S31 + 15u) / 31u);

    s16 Ptmp = (s16)(2 * (s16)L31 - (s16)Q);
    if (Ptmp < 0) Ptmp = 0; else if (Ptmp > 31) Ptmp = 31;
    u8 P = (u8)Ptmp;

    // 120 deg shift in 0..255 hue space = 85
    *r5 = hue2rgb(P, Q, (u8)(h + 85));
    *g5 = hue2rgb(P, Q, h);
    *b5 = hue2rgb(P, Q, (u8)(h - 85));
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

//* -------- Core palette application --------

static void ApplyOneRange(u16 pal16[16], u8 start, u8 len,
                          u8 hueShift8, u8 satAmtPct, u8 lumAmtPct,
                          bool8 svDownOnly, u8 dirH, u8 dirS, u8 dirL)
{
    if (len == 0) return;

    u8 iStart = ClampU8(start, 0, 15);
    u8 iEnd   = ClampU8((u16)start + (u16)len, 0, 16);

    // Compute signed deltas for S and L
    s8 sDelta = (s8)satAmtPct;
    s8 lDelta = (s8)lumAmtPct;
    if (svDownOnly) {
        sDelta = (s8)(-2 * sDelta);
        lDelta = (s8)(-2 * lDelta);
    } else {
        if (!dirS) sDelta = (s8)(-sDelta);
        if (!dirL) lDelta = (s8)(-lDelta);
    }

    for (u8 i = iStart; i < iEnd; ++i) {
        u8 r5, g5, b5, h, s, l;
        Rgb555Unpack(pal16[i], &r5, &g5, &b5);
        Rgb5ToHsl(r5, g5, b5, &h, &s, &l);

        h = dirH ? (u8)(h + hueShift8) : (u8)(h - hueShift8);

        s = ClampSignedToU8((s16)s + (s16)sDelta, 0, 100);
        l = ClampSignedToU8((s16)l + (s16)lDelta, 0, 100);

        HslToRgb5(h, s, l, &r5, &g5, &b5);
        pal16[i] = Rgb555Pack(r5, g5, b5);
    }
}

void ApplyVariantToPaletteBuffer(u32 species, bool8 shiny, u32 originalPID, u16 pal16[16])
{
    const struct SpeciesVariant *l = GetSpeciesVariants(species);
    if (l == NULL)
      return;

    // ---- Palette 1 (low 16 bits) ----
    if (l->pal1_hue_amount || l->pal1_sat_amount || l->pal1_lum_amount) {
        const u8 start = l->pal1_start;
        const u8 len   = (u8)(l->pal1_length + 1u); // stored as (len-1)
        const u8 hmax  = sHueTable[l->pal1_hue_amount & 7u];
        const u8 smax  = sSVTable[l->pal1_sat_amount & 3u];
        const u8 lmax  = sSVTable[l->pal1_lum_amount & 3u];

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
        const u8 len   = (u8)(l->pal2_length + 1u); // stored as (len-1)
        const u8 hmax  = sHueTable[l->pal2_hue_amount & 7u];
        const u8 smax  = sSVTable[l->pal2_sat_amount & 3u];
        const u8 lmax  = sSVTable[l->pal2_lum_amount & 3u];

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
