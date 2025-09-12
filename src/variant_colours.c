#include "variant_colours.h"
#include "trig.h"
#include "fpmath.h"

/* ---------- helpers ---------- */

typedef u8 uQ0_8;
typedef u16 uQ8_8;
typedef s16 sQ8_8;
typedef s32 sQ24_8;

#define IWRAM_CODE __attribute__((section(".iwram"), long_call))
#define IWRAM_CODE_DATA __attribute__((section(".iwram")))

// We already have some functions from syscalls:
// s16 Sin(s16 index, s16 amplitude) as Q8.8
// s16 Cos(s16 index, s16 amplitude) as Q8.8
// u16 Sqrt(u32 num)

#define Q8_8(n) ((s16)((n) >= 0 ? ( (n) * 256.0f + 0.5f ) \
                                  : ( (n) * 256.0f - 0.5f )))

static inline uQ8_8 mul_uq8_8(uQ8_8 a, uQ8_8 b) {
  u32 product = (a * b) + 0x80;
  return product >> 8;
}

static inline sQ8_8 mul_sq8_8(sQ8_8 a, sQ8_8 b) {
  s32 product = (a * b);
  product = product + (product < 0 ? -0x80 : 0x80);
  return product / 256;
}

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

static const IWRAM_CODE_DATA u8 cbrt_q0_8_lut[256] = {
      0,  40,  51,  58,  64,  69,  73,  77,  80,  84,  87,  89,  92,  95,  97,  99,
    101, 103, 105, 107, 109, 111, 113, 114, 116, 118, 119, 121, 122, 124, 125, 126,
    128, 129, 130, 132, 133, 134, 135, 136, 138, 139, 140, 141, 142, 143, 144, 145,
    146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160,
    161, 162, 163, 163, 164, 165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173,
    173, 174, 175, 175, 176, 177, 177, 178, 179, 180, 180, 181, 182, 182, 183, 183,
    184, 185, 185, 186, 187, 187, 188, 188, 189, 190, 190, 191, 191, 192, 193, 193,
    194, 194, 195, 196, 196, 197, 197, 198, 198, 199, 199, 200, 201, 201, 202, 202,
    203, 203, 204, 204, 205, 205, 206, 206, 207, 207, 208, 208, 209, 209, 210, 210,
    211, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 216, 216, 217, 217, 218,
    218, 219, 219, 220, 220, 221, 221, 221, 222, 222, 223, 223, 224, 224, 224, 225,
    225, 226, 226, 227, 227, 227, 228, 228, 229, 229, 230, 230, 230, 231, 231, 232,
    232, 232, 233, 233, 234, 234, 234, 235, 235, 236, 236, 236, 237, 237, 237, 238,
    238, 239, 239, 239, 240, 240, 241, 241, 241, 242, 242, 242, 243, 243, 243, 244,
    244, 245, 245, 245, 246, 246, 246, 247, 247, 247, 248, 248, 249, 249, 249, 250,
    250, 250, 251, 251, 251, 252, 252, 252, 253, 253, 253, 254, 254, 254, 255, 255,
};

// Cube root
static inline IWRAM_CODE u8 Cbrt(u8 x) {
  return cbrt_q0_8_lut[x];
}

static inline u8 Atan2(s16 y, s16 x)
{
    if ((x | y) == 0) return 0;
    // ArcTan2 returns unsigned angle (wrap-safe). Convert to 0..255
    u16 a = (u16)ArcTan2(y, x);
    return (u8)(a >> 8); // 0..255
}

/* -------- Colourspace conversions -------- */

static const IWRAM_CODE_DATA uQ0_8 rgb5_to_linear_rgb_q_0_8[32] = {
    0,   1,   1,   2,   4,   6,   8,  11,
   14,  18,  22,  26,  32,  38,  44,  51,
   59,  67,  76,  85,  96, 107, 118, 130,
  143, 158, 172, 187, 203, 221, 238, 255
};

static const IWRAM_CODE_DATA u8 linear_rgb_q0_8_to_rgb5[256] = {
   0,  2,  3,  3,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,
   9,  9,  9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12,
  12, 12, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15, 15,
  15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17,
  17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19,
  19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20,
  20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22,
  22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23,
  23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
  24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
  26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27,
  27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29,
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
  30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
  30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
};


// Based on https://bottosson.github.io/posts/oklab/#converting-from-linear-srgb-to-oklab

static inline void Rgb5ToOklch(u8 r5, u8 g5, u8 b5, uQ0_8 *L, uQ0_8 *C, uQ0_8 *H)
{
    //DebugPrintf("rgb5: %u %u %u", r5, g5, b5);

    // Step 1: Convert RGB555 to linear RGB Q0.8
    uQ8_8 r = rgb5_to_linear_rgb_q_0_8[r5];
    uQ8_8 g = rgb5_to_linear_rgb_q_0_8[g5];
    uQ8_8 b = rgb5_to_linear_rgb_q_0_8[b5];
    //DebugPrintf("rgb : %u %u %u", r, g, b);

    // Step 2: Calculate L, M & S
    uQ8_8 l = (mul_uq8_8(Q8_8(0.4122214708f), r)) + (mul_uq8_8(Q8_8(0.5363325363f), g)) + (mul_uq8_8(Q8_8(0.0514459929f), b));
    uQ8_8 m = (mul_uq8_8(Q8_8(0.2119034982f), r)) + (mul_uq8_8(Q8_8(0.6806995451f), g)) + (mul_uq8_8(Q8_8(0.1073969566f), b));
    uQ8_8 s = (mul_uq8_8(Q8_8(0.0883024619f), r)) + (mul_uq8_8(Q8_8(0.2817188376f), g)) + (mul_uq8_8(Q8_8(0.6299787005f), b));
    //DebugPrintf("lms : %u %u %u", l, m, s);

    // Step 3: Cube root
    uQ8_8 l_ = Cbrt(l & 0xFF);
    uQ8_8 m_ = Cbrt(m & 0xFF);
    uQ8_8 s_ = Cbrt(s & 0xFF);
    //DebugPrintf("lms_: %u %u %u", l_, m_, s_);

    // Step 4: Calculate unbounded LAB values in sQ8.8
    sQ8_8 Lu = ((s32)(mul_uq8_8(Q8_8(0.2104542553f), l_)) + (s32)(mul_uq8_8(Q8_8(0.7936177850f), m_)) - (s32)(mul_uq8_8(Q8_8(0.0040720468f), s_)));
    sQ8_8 Au = ((s32)(mul_uq8_8(Q8_8(1.9779984951f), l_)) - (s32)(mul_uq8_8(Q8_8(2.4285922050f), m_)) + (s32)(mul_uq8_8(Q8_8(0.4505937099f), s_)));
    sQ8_8 Bu = ((s32)(mul_uq8_8(Q8_8(0.0259040371f), l_)) + (s32)(mul_uq8_8(Q8_8(0.7827717662f), m_)) - (s32)(mul_uq8_8(Q8_8(0.8086757660f), s_)));
    //DebugPrintf("LAB : %d %d %d", Lu, Au, Bu);

    // Step 5: Calculate bounded CH values in Q8
    *L = ClampSignedToU8(Lu, 0, 255);
    *C = ClampU8(Sqrt((u32)(Au * Au + Bu * Bu)), 0, 255);
    if (*C == 0) *H = 0;
    else *H = Atan2(Bu, Au);
    //DebugPrintf("LCH : %u %u %u", *L, *C, *H);
}

static inline void OklchToRgb5(uQ0_8 L, uQ0_8 C, uQ0_8 H, u8 *r, u8 *g, u8 *b)
{
    //DebugPrintf("LCH : %u %u %u", L, C, H);

    // Step 1: CH to AB
    s32 A = Cos(H, C); // Default Cos & Sin return Q8_8
    s32 B = Sin(H, C);
    //DebugPrintf("AB  : %d %d ", A, B);

    // Step 2: Calculate l_ m_ s_ values
    sQ8_8 l_ = L + mul_sq8_8(Q8_8(0.3963377774f), A) + mul_sq8_8(Q8_8(0.2158037573f), B);
    sQ8_8 m_ = L - mul_sq8_8(Q8_8(0.1055613458f), A) - mul_sq8_8(Q8_8(0.0638541728f), B);
    sQ8_8 s_ = L - mul_sq8_8(Q8_8(0.0894841775f), A) - mul_sq8_8(Q8_8(1.2914855480f), B);
    //DebugPrintf("lms_: %d %d %d", l_, m_, s_);

    // Step 3: Cube back to l m s values
    uQ8_8 l = mul_sq8_8(mul_sq8_8(l_, l_), l_);
    sQ8_8 m = mul_sq8_8(mul_sq8_8(m_, m_), m_);
    sQ8_8 s = mul_sq8_8(mul_sq8_8(s_, s_), s_);
    //DebugPrintf("lms : %u %u %u", l, m, s);

    // Step 4: Calculate RGB values (unbounded)
    sQ8_8 ru =   mul_sq8_8(Q8_8(4.0767416621f), l) - mul_sq8_8(Q8_8(3.3077115913f), m) + mul_sq8_8(Q8_8(0.2309699292f), s);
    sQ8_8 gu = - mul_sq8_8(Q8_8(1.2684380046f), l) + mul_sq8_8(Q8_8(2.6097574011f), m) - mul_sq8_8(Q8_8(0.3413193965f), s);
    sQ8_8 bu = - mul_sq8_8(Q8_8(0.0041960863f), l) - mul_sq8_8(Q8_8(0.7034186147f), m) + mul_sq8_8(Q8_8(1.7076147010f), s);
    //DebugPrintf("rgbu: %u %u %u", ru, gu, bu);

    *r = linear_rgb_q0_8_to_rgb5[ClampSignedToU8(ru, 0, 255)];
    *g = linear_rgb_q0_8_to_rgb5[ClampSignedToU8(gu, 0, 255)];
    *b = linear_rgb_q0_8_to_rgb5[ClampSignedToU8(bu, 0, 255)];
    ///DebugPrintf("rgb : %u %u %u\n", *r, *g, *b);

}

/* -------- Variants data -------- */

const struct SpeciesVariant *GetSpeciesVariants(u32 species)
{
    const struct SpeciesVariant *l = &gSpeciesVariants[species];

    // Treat an all-zero entry as "no variant".
    if (l->pal1_length == 0 && l->pal2_length == 0 &&
        l->pal1_hue_amount == 0 && l->pal1_chr_amount == 0 && l->pal1_lum_amount == 0 &&
        l->pal2_hue_amount == 0 && l->pal2_chr_amount == 0 && l->pal2_lum_amount == 0)
    {
        DebugPrintf("%d - Default variant", species);
        static const struct SpeciesVariant s = DEFAULT_VARIANT;
        return &s;
    }
    else 
    {
        DebugPrintf("%d - Custom variant", species);
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
                          u8 hueShift8, u8 chrAmtPct, u8 lumAmtPct,
                          bool8 clDownOnly, u8 downH, u8 downC, u8 downL)
{
    if (len == 0) return;

    u8 iStart = ClampU8(start, 0, 15);
    u8 iEnd   = ClampU8((u16)start + (u16)len, 0, 15);

    // Compute signed deltas for S and L
    u8 cDelta = chrAmtPct;
    u8 lDelta = lumAmtPct;
    if (clDownOnly) {
        cDelta = (2 * cDelta);
        lDelta = (2 * lDelta);
    }

    for (u8 i = iStart; i <= iEnd; ++i) {
        u8 r5, g5, b5, h, c, l;
        Rgb555Unpack(pal16[i], &r5, &g5, &b5);
        Rgb5ToOklch(r5, g5, b5, &l, &c, &h);

        h = downH ? (u8)(h - hueShift8) : (u8)(h + hueShift8);
        h = 220;

        if (clDownOnly || downC) c = cDelta > c ? 0 : c - cDelta;
        else c = c + cDelta;
        c *= 2;
        if (clDownOnly || downL) l = lDelta > l ? 0 : l - lDelta;
        else l = (255 - lDelta < l) ? 255 : l + lDelta;
        l /= 2;

        OklchToRgb5(l, c, h, &r5, &g5, &b5);
        pal16[i] = Rgb555Pack(r5, g5, b5);
    }
}

void ApplyVariantToPaletteBuffer(u32 species, bool8 shiny, u32 originalPID, u16 pal16[16])
{
    const struct SpeciesVariant *l = GetSpeciesVariants(species);
    if (l == NULL)
      return;

    // ---- Palette 1 (low 16 bits) ----
    if (l->pal1_hue_amount || l->pal1_chr_amount || l->pal1_lum_amount) {
        const u8 start = l->pal1_start;
        const u8 len   = (u8)(l->pal1_length + 1u); // stored as (len-1)
        const u8 hmax  = sHueTable[l->pal1_hue_amount & 7u];
        const u8 cmax  = sCLTable[l->pal1_chr_amount & 3u];
        const u8 lmax  = sCLTable[l->pal1_lum_amount & 3u];

        u8 hue = ScaleBitsToMax(BITS(originalPID, 0, 7),  hmax, 7);
        u8 chr = ScaleBitsToMax(BITS(originalPID, 7, 3),  cmax, 3);
        u8 lum = ScaleBitsToMax(BITS(originalPID, 10, 3), lmax, 3);
        u8 downH = (u8)BITS(originalPID, 13, 1);
        u8 downC = (u8)BITS(originalPID, 14, 1);
        u8 downL = (u8)BITS(originalPID, 15, 1);

        ApplyOneRange(
          pal16, start, len,
          hue, chr, lum, l->pal1_sv_down_only,
          downH, downC, downL
        );
    }

    // ---- Palette 2 (high 16 bits) ----
    if (l->pal2_hue_amount || l->pal2_chr_amount || l->pal2_lum_amount) {
        const u8 start = l->pal2_start;
        const u8 len   = (u8)(l->pal2_length + 1u); // stored as (len-1)
        const u8 hmax  = sHueTable[l->pal2_hue_amount & 7u];
        const u8 cmax  = sCLTable[l->pal2_chr_amount & 3u];
        const u8 lmax  = sCLTable[l->pal2_lum_amount & 3u];

        // Extract bits: [16..22]=h, [23..25]=c, [26..28]=l, [29]=downH, [30]=downC, [31]=downL
        u8 hue = ScaleBitsToMax(BITS(originalPID, 16, 7), hmax, 7);
        u8 chr = ScaleBitsToMax(BITS(originalPID, 23, 3), cmax, 3);
        u8 lum = ScaleBitsToMax(BITS(originalPID, 26, 3), lmax, 3);
        u8 downH = (u8)BITS(originalPID, 29, 1);
        u8 downC = (u8)BITS(originalPID, 30, 1);
        u8 downL = (u8)BITS(originalPID, 31, 1);

        ApplyOneRange(
          pal16, start, len,
          hue, chr, lum, l->pal2_sv_down_only,
          downH, downC, downL
        );
    }
}
