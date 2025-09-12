#include "global.h"
#include "pokemon.h"
#include "gba/isagbprint.h"   // <— needed for MgbaPrintf
#include "mini_printf.h"
#include "_debug_utils.h"
#include "daycare.h"
#include "constants/characters.h"
#include "constants/moves.h"
#include <stdarg.h>
#include <stddef.h>

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef NDEBUG

static const u8 sSubstructOrder[24][4] = {
    {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},
    {1,0,2,3},{1,0,3,2},{2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},
    {1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},{2,3,0,1},{3,2,0,1},
    {1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0},
};

// ---------------------------------------------------------------------
// small local utils (no libc deps)
// ---------------------------------------------------------------------
static inline void mem_cpy(u8 *d, const u8 *s, size_t n){ while (n--) *d++ = *s++; }
static inline void mem_set(u8 *d, u8 v, size_t n){ while (n--) *d++ = v; }

// mini_vsnprintf wrapper (truncate-safe)
static inline int mini_snprintf_wrap(char *buffer, u32 buffer_len,
                                     const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    s32 n = mini_vsnprintf(buffer, buffer_len, fmt, va);
    va_end(va);
    if (n < 0) return 0;
    if ((u32)n >= buffer_len) return (int)buffer_len - 1;  // truncated
    return (int)n;
}

static void DumpHexU8(int level, const char *label, const u8 *buf, size_t len)
{
    char line[96];
    MgbaPrintf(level, "%s (len=%u):", label, (unsigned)len);
    for (size_t i = 0; i < len; i += 16)
    {
        size_t n = (len - i > 16) ? 16 : (len - i);
        int pos = mini_snprintf_wrap(line, sizeof(line), "  %03u: ", (unsigned)i);
        for (size_t j = 0; j < n && pos < (int)sizeof(line); j++)
            pos += mini_snprintf_wrap(line + pos, sizeof(line) - pos, "%02X ", buf[i + j]);
        MgbaPrintf(level, "%s", line);
    }
}


// Bit writer/reader (LSB-first)
typedef struct { u8 *buf; size_t cap; size_t bit; } BitW;
typedef struct { const u8 *buf; size_t len; size_t bit; u8 err; } BitR;

static void bw_init(BitW *w, u8 *buf, size_t cap){ w->buf = buf; w->cap = cap; w->bit = 0; mem_set(buf,0,cap); }
static u8 bw_put(BitW *w, u32 v, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
    {
        size_t byte = w->bit >> 3; if (byte >= w->cap) return 0;
        unsigned bit = w->bit & 7;
        u8 b = w->buf[byte];
        b = (u8)((b & ~(1u<<bit)) | (((v>>i)&1u) << bit));
        w->buf[byte] = b;
        w->bit++;
    }
    return 1;
}
static size_t bw_bytes(const BitW *w){ return (w->bit + 7) >> 3; }

static void br_init(BitR *r, const u8 *buf, size_t len){ r->buf = buf; r->len = len; r->bit = 0; r->err = 0; }
static u32 br_get(BitR *r, unsigned n)
{
    u32 v = 0;
    for (unsigned i=0;i<n;i++)
    {
        size_t byte = r->bit >> 3; if (byte >= r->len){ r->err = 1; return v; }
        unsigned bit = r->bit & 7;
        v |= ((u32)((r->buf[byte] >> bit) & 1u)) << i;
        r->bit++;
    }
    return v;
}

// Secure area helpers
static void decrypt_box(const struct BoxPokemon *b, u32 *outWords, u32 words)
{
    u32 key = b->personality ^ b->otId;
    const u32 *src = (const u32 *)b->secure.raw;
    for (u32 i=0;i<words;i++) outWords[i] = src[i] ^ key;
}
static void encrypt_box(struct BoxPokemon *b, const u32 *inWords, u32 words)
{
    u32 key = b->personality ^ b->otId;
    u32 *dst = (u32 *)b->secure.raw;
    for (u32 i=0;i<words;i++) dst[i] = inWords[i] ^ key;
}
static u16 calc_secure_checksum(const union PokemonSubstruct *s)
{
    const u16 *p = (const u16 *)s;
    const size_t count = (NUM_SUBSTRUCT_BYTES * 4) / 2; // all 4 substructs
    u32 sum = 0;
    for (size_t i=0;i<count;i++) sum += p[i];
    return (u16)sum;
}

// Extract names (using Gen3 0xFF terminator semantics)
static u8 get_nickname12(const struct BoxPokemon *b, u8 out[POKEMON_NAME_LENGTH])
{
    for (int i=0;i<10;i++) out[i] = b->nickname[i];
    enum { WORDS = (NUM_SUBSTRUCT_BYTES * 4) / 4 };
    u32 tmp[WORDS]; decrypt_box(b, tmp, WORDS);
    const union PokemonSubstruct *dec = (const union PokemonSubstruct *)tmp;
    const u8 *ord = sSubstructOrder[b->personality % 24];
    int slot0=-1; for (int s=0;s<4;s++) if (ord[s]==0) { slot0 = s; break; }
    const struct PokemonSubstruct0 *s0 = &dec[slot0].type0;
    out[10] = (u8)s0->nickname11;
    out[11] = (u8)s0->nickname12;
    int len = 12;
    for (int i=0;i<12;i++){ if (out[i]==0xFF){ len = i; break; } }
    return (u8)len;
}
static u8 get_otname(const struct BoxPokemon *b, u8 out[], u8 maxLen)
{
    int len = maxLen;
    for (int i=0;i<maxLen;i++){ out[i]=b->otName[i]; if (out[i]==0xFF){ len = i; break; } }
    return (u8)len;
}

// ---------------- text helpers: ASCII<->game string + 6-bit alphabet ----------------

// GF bytes (0x.. codes) <-> ASCII for the common set we 6-bit pack.
 // returns length
static u8 DecodeBoxStringToAscii(const u8 *src, u8 *dst, u8 maxOut)
{
    u8 n = 0;
    for (; n < maxOut && src[n] != EOS; n++)
    {
        u8 c = src[n];
        if (c == CHAR_SPACE)                         dst[n] = ' ';
        else if (c >= CHAR_A && c <= CHAR_Z)         dst[n] = (u8)('A' + (c - CHAR_A));
        else if (c >= CHAR_a && c <= CHAR_z)         dst[n] = (u8)('a' + (c - CHAR_a));
        else if (c >= CHAR_0 && c <= CHAR_9)         dst[n] = (u8)('0' + (c - CHAR_0));
        else                                         dst[n] = '?';   // unknown glyph
    }
    return n;
}

 // returns bytes written (excl. EOS)
static u8 EncodeAsciiToBoxString(const u8 *ascii, u8 len, u8 *dst, u8 maxOut)
{
    u8 n = (len > maxOut) ? maxOut : len;
    for (u8 i = 0; i < n; i++)
    {
        u8 a = ascii[i];
        if (a == ' ')                              dst[i] = CHAR_SPACE;
        else if (a >= 'A' && a <= 'Z')             dst[i] = (u8)(CHAR_A + (a - 'A'));
        else if (a >= 'a' && a <= 'z')             dst[i] = (u8)(CHAR_a + (a - 'a'));
        else if (a >= '0' && a <= '9')             dst[i] = (u8)(CHAR_0 + (a - '0'));
        else                                       dst[i] = CHAR_SPACE;   // or map to some fallback glyph
    }
    if (n < maxOut) dst[n] = EOS;
    return n;
}

// 6-bit 'common' alphabet
static inline u8 ascii_to_six(u8 c)   // returns 0..63 or 0xFF if not representable
{
    if (c >= 'A' && c <= 'Z') return (u8)(c - 'A');               // 0..25
    if (c >= 'a' && c <= 'z') return (u8)(26 + c - 'a');          // 26..51
    if (c >= '0' && c <= '9') return (u8)(52 + c - '0');          // 52..61
    if (c == ' ')             return 62;
    return 0xFF;
}
static inline u8 six_to_ascii(u8 v)   // 0..63 -> ASCII
{
    if (v < 26)         return (u8)('A' + v);
    else if (v < 52)    return (u8)('a' + (v - 26));
    else if (v < 62)    return (u8)('0' + (v - 52));
    else if (v == 62)   return ' ';
    else                return '?';
}

// Write nickname/OT with 6-bit (base64url) OR raw 8-bit fallback.
// Layout:
//   mode:1            0=6bit, 1=raw8
//   payload: len×6 or len×8
static u8 name_can_use_6bit(const u8 *ascii, u8 len)
{
    for (u8 i = 0; i < len; i++) if (ascii_to_six(ascii[i]) == 0xFF) return 0;
    return 1;
}

static u8 bw_put_name6or8(BitW *w, const u8 *ascii, u8 len)
{
    u8 use6 = name_can_use_6bit(ascii, len);
    if (!bw_put(w, use6 ? 0u : 1u, 1)) return 0;    // mode

    if (use6)
    {
        for (u8 i = 0; i < len; i++)
        {
            u8 v = ascii_to_six(ascii[i]);
            if (v == 0xFF) return 0;
            if (!bw_put(w, v, 6)) return 0;
        }
        if (!bw_put(w, 63, 6)) return 0; // Terminator
    }
    else
    {
        for (u8 i = 0; i < len; i++)
        {
            if (!bw_put(w, ascii[i], 8)) return 0;
        }
        if (!bw_put(w, 0xFF, 8)) return 0; // Terminator
    }
    return 1;
}

static u8 br_get_name6or8(BitR *r, u8 *outAscii, u8 outMax, u8 *outLen)
{
    u8 mode = (u8)br_get(r, 1); // 0 = 6-bit, 1 = raw-8
    u8 n = 0;

    if (mode == 0) {
        for (;;) {
            u8 v = (u8)br_get(r, 6);
            if (r->err) { *outLen = n; return 0; }
            if (v == 63) break;               // 6-bit EOS
            if (n < outMax) outAscii[n++] = six_to_ascii(v);
        }
    } else {
        for (;;) {
            u8 b = (u8)br_get(r, 8);
            if (r->err) { *outLen = n; return 0; }
            if (b == 0xFF) break;             // raw-8 EOS
            if (n < outMax) outAscii[n++] = b;
        }
    }

    *outLen = n;
    return 1;
}

// --- species name access (GF charset, EOS-terminated) ---
static const u8 *SpeciesNameGF(u16 species)
{
    return gSpeciesInfo[species].speciesName;
}

// Copy species name into a 12-byte "nickname storage" buffer (10 + 2 split), return length (<=12)
static u8 SpeciesNameToNick12(u16 species, u8 out12[POKEMON_NAME_LENGTH])
{
    const u8 *src = SpeciesNameGF(species);
    u8 n = 0;
    while (n < 12 && src[n] != EOS) { out12[n] = src[n]; n++; }
    if (n < 12) out12[n] = EOS; // ensure EOS exists somewhere
    return n;
}

// Compare Box nickname (12-byte storage) to species name in GF bytes (EOS semantics)
static bool8 BoxNicknameEqualsSpecies(const struct BoxPokemon *b, u16 species)
{
    u8 nick12[POKEMON_NAME_LENGTH];
    u8 nickLen = get_nickname12(b, nick12);   // respects EOS in 10+2 layout

    const u8 *sp = SpeciesNameGF(species);
    u8 spLen = 0;
    while (spLen < 12 && sp[spLen] != EOS) spLen++;

    if (nickLen != spLen) return FALSE;
    for (u8 i = 0; i < nickLen; i++)
        if (nick12[i] != sp[i]) return FALSE;
    return TRUE;
}

// Install a 12-byte "nickname" (GF charset) into BoxPokemon + substruct0 split fields
static void ApplyNick12ToBox(struct BoxPokemon *b, struct PokemonSubstruct0 *s0, const u8 nick12[POKEMON_NAME_LENGTH], u8 len)
{
    // First 10 live in BoxPokemon
    for (int i = 0; i < 10; i++)
        b->nickname[i] = (i < len) ? nick12[i] : EOS;

    // Remaining 2 live inside substruct0
    s0->nickname11 = (len > 10) ? nick12[10] : EOS;
    s0->nickname12 = (len > 11) ? nick12[11] : EOS;
}

static void DumpHexU32(int level, const char *label, const u32 *buf, size_t count)
{
    char line[128];
    MgbaPrintf(level, "%s (u32 words=%u):", label, (unsigned)count);
    for (size_t i = 0; i < count; i += 8)
    {
        size_t n = (count - i > 8) ? 8 : (count - i);
        int pos = mini_snprintf_wrap(line, sizeof(line), "  %03u: ", (unsigned)i);
        for (size_t j = 0; j < n && pos < (int)sizeof(line); j++)
            pos += mini_snprintf_wrap(line + pos, sizeof(line) - pos, "%08X ", (unsigned)buf[i + j]);
        MgbaPrintf(level, "%s", line);
    }
}

static void DumpSubstruct0(int level, const struct PokemonSubstruct0 *s)
{
    MgbaPrintf(level, "  Substruct0 (Growth) {");
    MgbaPrintf(level, "    species=%u, teraType=%u, heldItem=%u", s->species, s->teraType, s->heldItem);
    MgbaPrintf(level, "    experience=%u, ppBonuses=0x%02X, friendship=%u", s->experience, s->ppBonuses, s->friendship);
    MgbaPrintf(level, "    pokeball=%u, nickname11=%u, nickname12=%u", s->pokeball, s->nickname11, s->nickname12);
    MgbaPrintf(level, "  }");
}

static void DumpSubstruct1(int level, const struct PokemonSubstruct1 *s)
{
    MgbaPrintf(level, "  Substruct1 (Attacks/PP/HyperTrain) {");
    MgbaPrintf(level, "    move1=%u, move2=%u, move3=%u, move4=%u",
               s->move1, s->move2, s->move3, s->move4);
    MgbaPrintf(level, "    pp1=%u, pp2=%u, pp3=%u, pp4=%u",
               s->pp1, s->pp2, s->pp3, s->pp4);
    MgbaPrintf(level, "    HT: HP=%u Atk=%u Def=%u Spe=%u SpA=%u SpD=%u",
               s->hyperTrainedHP, s->hyperTrainedAttack, s->hyperTrainedDefense,
               s->hyperTrainedSpeed, s->hyperTrainedSpAttack, s->hyperTrainedSpDefense);
    MgbaPrintf(level, "    evoTrack1=%u, evoTrack2=%u", s->evolutionTracker1, s->evolutionTracker2);
    MgbaPrintf(level, "  }");
}

static void DumpSubstruct2(int level, const struct PokemonSubstruct2 *s)
{
    MgbaPrintf(level, "  Substruct2 (EVs/Contest) {");
    MgbaPrintf(level, "    EVs: HP=%u Atk=%u Def=%u Spe=%u SpA=%u SpD=%u",
               s->hpEV, s->attackEV, s->defenseEV, s->speedEV, s->spAttackEV, s->spDefenseEV);
    MgbaPrintf(level, "    Contest: cool=%u beauty=%u cute=%u smart=%u tough=%u sheen=%u",
               s->cool, s->beauty, s->cute, s->smart, s->tough, s->sheen);
    MgbaPrintf(level, "  }");
}

static void DumpSubstruct3(int level, const struct PokemonSubstruct3 *s)
{
    MgbaPrintf(level, "  Substruct3 (Misc/IVs/Ribbons) {");
    MgbaPrintf(level, "    pokerus=0x%02X, metLocation=%u, metLevel=%u, metGame=%u, dynamaxLevel=%u, otGender=%u",
               s->pokerus, s->metLocation, s->metLevel, s->metGame, s->dynamaxLevel, s->otGender);
    MgbaPrintf(level, "    IVs: HP=%u Atk=%u Def=%u Spe=%u SpA=%u SpD=%u",
               s->hpIV, s->attackIV, s->defenseIV, s->speedIV, s->spAttackIV, s->spDefenseIV);
    MgbaPrintf(level, "    flags: isEgg=%u gMax=%u isShadow=%u abilityNum=%u fateful=%u",
               s->isEgg, s->gigantamaxFactor, s->isShadow, s->abilityNum, s->modernFatefulEncounter);
    MgbaPrintf(level, "    ribbons: cool=%u beauty=%u cute=%u smart=%u tough=%u | champ=%u win=%u vict=%u artist=%u effort=%u",
               s->coolRibbon, s->beautyRibbon, s->cuteRibbon, s->smartRibbon, s->toughRibbon,
               s->championRibbon, s->winningRibbon, s->victoryRibbon, s->artistRibbon, s->effortRibbon);
    MgbaPrintf(level, "    ribbons: marine=%u land=%u sky=%u country=%u national=%u earth=%u world=%u",
               s->marineRibbon, s->landRibbon, s->skyRibbon, s->countryRibbon,
               s->nationalRibbon, s->earthRibbon, s->worldRibbon);
    MgbaPrintf(level, "  }");
}

static void DumpDecryptedSubstructs(int level, const struct BoxPokemon *b)
{
    u32 key = b->personality ^ b->otId;

    enum { WORDS = (NUM_SUBSTRUCT_BYTES * 4) / 4 };
    u32 tmp[WORDS];
    const u32 *src = (const u32 *)b->secure.raw;
    for (u32 i = 0; i < WORDS; i++)
        tmp[i] = src[i] ^ key;

    const union PokemonSubstruct *dec = (const union PokemonSubstruct *)tmp;

    const u8 *ord = sSubstructOrder[b->personality % 24];
    int idxOf[4] = {-1,-1,-1,-1};
    for (int slot = 0; slot < 4; slot++)
        idxOf[ord[slot]] = slot;

    MgbaPrintf(level, "  secure (decrypted) { key=0x%08X, order=[%u,%u,%u,%u] }",
               (unsigned)key, ord[0], ord[1], ord[2], ord[3]);

    DumpSubstruct0(level, &dec[idxOf[0]].type0);
    DumpSubstruct1(level, &dec[idxOf[1]].type1);
    DumpSubstruct2(level, &dec[idxOf[2]].type2);
    DumpSubstruct3(level, &dec[idxOf[3]].type3);
}

static void DumpNameAsciiLine(int level, const char *label, const u8 *gfBytes, u8 maxLen)
{
    char ascii[32];
    u8 n = DecodeBoxStringToAscii(gfBytes, (u8 *)ascii, maxLen);
    if (n >= sizeof(ascii)) n = sizeof(ascii) - 1;
    ascii[n] = '\0';
    MgbaPrintf(level, "  %s (text): \"%s\"", label, ascii);
}

static void DumpBoxPokemon(int level, const struct BoxPokemon *b)
{
    MgbaPrintf(level, "BoxPokemon {");
    MgbaPrintf(level, "  personality: 0x%08X", (unsigned)b->personality);
    MgbaPrintf(level, "  otId:        0x%08X", (unsigned)b->otId);

    u8 nick12[POKEMON_NAME_LENGTH];
    (void)get_nickname12(b, nick12);
    DumpNameAsciiLine(level, "nickname", nick12, POKEMON_NAME_LENGTH);
    DumpHexU8(level, "  nickname", b->nickname, ARRAY_COUNT(b->nickname));

    MgbaPrintf(level, "  language: %u", b->language);
    MgbaPrintf(level, "  hiddenNatureModifier: %u", b->hiddenNatureModifier);

    MgbaPrintf(level, "  isBadEgg: %u", b->isBadEgg);
    MgbaPrintf(level, "  hasSpecies: %u", b->hasSpecies);
    MgbaPrintf(level, "  isEgg: %u", b->isEgg);
    MgbaPrintf(level, "  blockBoxRS: %u", b->blockBoxRS);
    MgbaPrintf(level, "  daysSinceFormChange: %u", b->daysSinceFormChange);
    MgbaPrintf(level, "  unused_13: %u", b->unused_13);

    u8 otRaw[PLAYER_NAME_LENGTH];
    (void)get_otname(b, otRaw, PLAYER_NAME_LENGTH);
    DumpNameAsciiLine(level, "otName", otRaw, PLAYER_NAME_LENGTH);
    DumpHexU8(level, "  otName", b->otName, ARRAY_COUNT(b->otName));

    MgbaPrintf(level, "  markings: %u", b->markings);
    MgbaPrintf(level, "  compressedStatus: %u", b->compressedStatus);
    MgbaPrintf(level, "  checksum: 0x%04X", b->checksum);

    MgbaPrintf(level, "  hpLost: %u", b->hpLost);
    MgbaPrintf(level, "  shinyModifier: %u", b->shinyModifier);
    MgbaPrintf(level, "  unused_1E: %u", b->unused_1E);

    DumpDecryptedSubstructs(level, b);

    DumpHexU32(level, "  secure.raw",
               b->secure.raw,
               ARRAY_COUNT(b->secure.raw));
    MgbaPrintf(level, "}");
}



// CRC16-CCITT (0xFFFF, poly 0x1021)
static u16 crc16_ccitt(const u8 *p, size_t n)
{
    u16 crc = 0xFFFF;
    while (n--)
    {
        crc ^= (u16)(*p++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
    }
    return crc;
}

// Base64url (no padding)
static const char sB64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64url_encode(const u8 *in, size_t inLen, char *out, size_t outCap)
{
    size_t o = 0;
    for (size_t i = 0; i < inLen; i += 3)
    {
        u32 v = in[i] << 16;
        if (i + 1 < inLen) v |= in[i+1] << 8;
        if (i + 2 < inLen) v |= in[i+2];

        char c1 = sB64Url[(v >> 18) & 63];
        char c2 = sB64Url[(v >> 12) & 63];
        char c3 = (i + 1 < inLen) ? sB64Url[(v >> 6) & 63] : 0;
        char c4 = (i + 2 < inLen) ? sB64Url[(v >> 0) & 63] : 0;

        if (o + 2 > outCap) return 0; out[o++] = c1; out[o++] = c2;
        if (c3){ if (o + 1 > outCap) return 0; out[o++] = c3; }
        if (c4){ if (o + 1 > outCap) return 0; out[o++] = c4; }
    }
    if (o < outCap) out[o] = 0;
    return o;
}

static u8 b64url_rev(char c)
{
    if (c >= 'A' && c <= 'Z') return (u8)(c - 'A');
    if (c >= 'a' && c <= 'z') return (u8)(26 + c - 'a');
    if (c >= '0' && c <= '9') return (u8)(52 + c - '0');
    if (c == '+') return 62; // '+'
    if (c == '/') return 63; // '/'
    return 0xFF;
}

static size_t b64url_decode(const char *in, u8 *out, size_t outCap)
{
    size_t o = 0;
    u32 acc = 0; int bits = 0;
    for (const char *p = in; *p; ++p)
    {
        u8 v = b64url_rev(*p);
        if (v == 0xFF) return 0;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= outCap) return 0;
            out[o++] = (u8)((acc >> bits) & 0xFF);
        }
    }
    return o;
}

// ---------------- v1 wire format ----------------
#define TRADE_V1        1
#define TRADE_NICK_MAX  12
#define TRADE_OT_MAX    PLAYER_NAME_LENGTH

// ---- helpers for compact IV coding (masks + 5b values) ----
static void ivs_to_masks_vals(const struct PokemonSubstruct3 *s3,
                              u8 *maskNon31, u8 *maskNonZero,
                              u8 vals[6], u8 *valsCount)
{
    u8 iv[6] = {
        (u8)s3->hpIV, (u8)s3->attackIV, (u8)s3->defenseIV,
        (u8)s3->speedIV, (u8)s3->spAttackIV, (u8)s3->spDefenseIV
    };
    u8 m31 = 0, m0 = 0, n = 0;
    for (u32 i=0;i<6;i++) if (iv[i] != 31) m31 |= (1u<<i);
    for (u32 i=0;i<6;i++) if ((m31 & (1u<<i)) && iv[i] != 0) m0 |= (1u<<i);
    for (u32 i=0;i<6;i++) if ((m31 & (1u<<i)) && (m0 & (1u<<i))) vals[n++] = (u8)(iv[i] & 31);
    *maskNon31 = m31; *maskNonZero = m0; *valsCount = n;
}

static void ivs_from_masks_vals(u8 maskNon31, u8 maskNonZero,
                                const u8 *vals, u8 out[6])
{
    for (u32 i=0;i<6;i++) out[i] = 31;
    u8 k = 0;
    for (u32 i=0;i<6;i++)
    {
        if (maskNon31 & (1u<<i))
            out[i] = (maskNonZero & (1u<<i)) ? (vals[k++] & 31) : 0;
    }
}

enum { EV_ENC_ALLZERO = 0, EV_ENC_COMP252 = 1, EV_ENC_SPARSE = 2 };

static inline bool8 evq_all_zero(const u8 evQ[6])
{
    for (int i = 0; i < 6; i++) if (evQ[i] != 0) return FALSE;
    return TRUE;
}

// Detect exactly: two stats = 63 and one stat = 1; all others = 0
static bool8 evq_try_comp252(const u8 evQ[6], u8 *outPairMask, u8 *outSmallIdx)
{
    u8 nMax = 0, maxMask = 0, smallIdx = 0xFF;
    for (int i = 0; i < 6; i++)
    {
        if (evQ[i] == 63) { maxMask |= (1u << i); nMax++; }
        else if (evQ[i] == 1) { if (smallIdx != 0xFF) return FALSE; smallIdx = (u8)i; }
        else if (evQ[i] != 0) return FALSE; // anything else breaks the COMP case
    }
    if (nMax == 2 && smallIdx != 0xFF)
    {
        *outPairMask = maxMask;
        *outSmallIdx = smallIdx;
        return TRUE;
    }
    return FALSE;
}

// Write compressed EVs
static bool8 bw_put_evs(BitW *w, const u8 evQ[6])
{
    // Mode select
    if (evq_all_zero(evQ))
    {
        // mode (2b) = 00
        if (!bw_put(w, EV_ENC_ALLZERO, 2)) return FALSE;
        return TRUE;
    }

    u8 pairMask = 0, smallIdx = 0;
    if (evq_try_comp252(evQ, &pairMask, &smallIdx))
    {
        // mode (2b) = 01, then pairMask(6b), smallIdx(3b)
        if (!bw_put(w, EV_ENC_COMP252, 2)) return FALSE;
        if (!bw_put(w, pairMask & 0x3Fu, 6)) return FALSE;
        if (!bw_put(w, smallIdx & 0x07u, 3)) return FALSE;
        return TRUE;
    }

    // General sparse: mode (2b) = 10, maskNonZero(6b), then values(6b each)
    if (!bw_put(w, EV_ENC_SPARSE, 2)) return FALSE;

    u8 mask = 0;
    for (int i = 0; i < 6; i++) if (evQ[i] != 0) mask |= (1u << i);
    if (!bw_put(w, mask & 0x3Fu, 6)) return FALSE;
    for (int i = 0; i < 6; i++)
        if (mask & (1u << i))
            if (!bw_put(w, evQ[i] & 63u, 6)) return FALSE;

    return TRUE;
}

// Read compressed EVs
static bool8 br_get_evs(BitR *r, u8 outEvQ[6])
{
    u8 mode = (u8)br_get(r, 2);
    for (int i = 0; i < 6; i++) outEvQ[i] = 0; // default zero

    if (mode == EV_ENC_ALLZERO)
    {
        return r->err ? FALSE : TRUE;
    }
    else if (mode == EV_ENC_COMP252)
    {
        u8 pairMask = (u8)br_get(r, 6);
        u8 smallIdx = (u8)br_get(r, 3);
        if (r->err) return FALSE;
        for (int i = 0; i < 6; i++) if (pairMask & (1u << i)) outEvQ[i] = 63;
        if (smallIdx < 6 && !(pairMask & (1u << smallIdx))) outEvQ[smallIdx] = 1;
        return TRUE;
    }
    else if (mode == EV_ENC_SPARSE)
    {
        u8 mask = (u8)br_get(r, 6);
        if (r->err) return FALSE;
        for (int i = 0; i < 6; i++)
            if (mask & (1u << i))
                outEvQ[i] = (u8)br_get(r, 6);
        return r->err ? FALSE : TRUE;
    }

    // Unknown mode
    r->err = 1;
    return FALSE;
}

// ---------- Per-species move list (built at runtime) ----------
#define MOVE_LOCAL_BITS     7
#define MOVE_LOCAL_ESCAPE   ((1u << MOVE_LOCAL_BITS) - 1)
#define MOVE_LIST_CAP       127

// Some repos use MOVE_NONE=0; many lists also end with 0xFFFF. Accept both.
static inline bool8 MoveListEnd(u16 m) { return (m == LEVEL_UP_MOVE_END || m == MOVE_UNAVAILABLE || m == MOVE_NONE); }

// Small helpers
static inline bool8 u16_contains(const u16 *a, u8 n, u16 v)
{
    for (u8 i = 0; i < n; i++) if (a[i] == v) return TRUE;
    return FALSE;
}

static void u16_isort(u16 *a, u8 n)   // stable enough, n ≤ 127
{
    for (u8 i = 1; i < n; i++)
    {
        u16 x = a[i]; u8 j = i;
        while (j && a[j-1] > x) { a[j] = a[j-1]; j--; }
        a[j] = x;
    }
}

// Build union of level-up, teachable, and (pre-evo) egg moves.
// Returns count (≤127) and fills 'out' sorted ascending and deduped.
static u8 Trade_BuildSpeciesMoveList(u16 species, u16 out[MOVE_LIST_CAP])
{
    u8 n = 0;

    // 1) Level-up (this species)
    const struct LevelUpMove *lvl = GetSpeciesLevelUpLearnset(species);
    for (; !MoveListEnd(lvl->move); lvl++)
    {
        u16 m = lvl->move;
        if (m != MOVE_NONE && !u16_contains(out, n, m) && n < MOVE_LIST_CAP) out[n++] = m;
    }

    // 2) Teachable (this species)
    const u16 *teach = GetSpeciesTeachableLearnset(species);
    for (; !MoveListEnd(*teach); teach++)
    {
        u16 m = *teach;
        if (m != MOVE_NONE && !u16_contains(out, n, m) && n < MOVE_LIST_CAP) out[n++] = m;
    }

    // 3) Egg moves (use egg species for backtracking to lowest hatchable form)
    {
        u16 eggSp = GetEggSpecies(species);
        const u16 *egg = GetSpeciesEggMoves(eggSp);
        for (; !MoveListEnd(*egg); egg++)
        {
            u16 m = *egg;
            if (m != MOVE_NONE && !u16_contains(out, n, m) && n < MOVE_LIST_CAP) out[n++] = m;
        }
    }

    // Sort for stable indices (and to enable binary search if desired)
    u16_isort(out, n);
    return n;
}

// Map global move → per-species 7-bit index; 0xFF if not representable.
static u8 Trade_MoveToLocalIndex(u16 species, u16 move)
{
    if (move == MOVE_NONE) return 0xFF;
    u16 list[MOVE_LIST_CAP];
    u8 len = Trade_BuildSpeciesMoveList(species, list);

    // Linear search is fine for ≤127; switch to binary if you prefer.
    for (u8 i = 0; i < len; i++) if (list[i] == move) return i;
    return 0xFF; // not in table -> must use escape + raw 11b move
}

// Map per-species 7-bit index → global move. Returns FALSE if invalid.
static bool8 Trade_LocalIndexToMove(u16 species, u8 idx, u16 *outMove)
{
    u16 list[MOVE_LIST_CAP];
    u8 len = Trade_BuildSpeciesMoveList(species, list);
    if (idx >= len) return FALSE;
    *outMove = list[idx];
    return TRUE;
}




// ---------------- v1 wire format (lean) ----------------
// Bit writer is LSB-first (matches your existing BitW/BitR).
//
// Header:
//   ver:5  = 1
//   flags:8  (bit0 HAS_NICKNAME, bit1 HAS_TERA, bit2 HAS_DMAX, bit3 GMAX,
//             bit4 HAS_PPUPS, bit5 SHINY_WANTED, bit6 NATURE_OVR, bit7 IS_EGG)
//
// Core:
//   species:11   (Max: 1524)
//   level:7      (Max: 100)
//   pid:32
//   otId:32
//   abilityNum:2
//   [if NATURE_OVR] nature:5
//
// Moves:
//   moveCountMinus1:2
//   moves[ count ]: each 11    (Max: 933)
//   [if HAS_PPUPS] ppUps[ count ]: each 2
//
// EVs (/4), compressed:
//   evMode:2
//     00 = ALLZERO              -> (no further EV bits)
//     01 = COMP252 (252/252/4): -> pairMask:6 (two stats = 63), smallIdx:3 (one stat = 1)
//     10 = SPARSE:              -> maskNonZero:6, then for each set bit evQ[i]:6
//     11 = (reserved)
//
// IVs (compact):
//   maskNon31:6
//   maskNonZero:6
//   values: 5b per stat where both masks have bit set
//
// Optionals:
//   [if HAS_TERA] teraType:5
//   [if HAS_DMAX] dmaxLevel:4
//
// Friendship:
//   friendCustom:1
//   if friendCustom=1 -> friendVal:8
//   else              -> friendFull:1  (1=255,0=0)
//
// Nickname (present iff HAS_NICKNAME and nickname != species):
//   nameMode:1              0=6-bit alphabet, 1=raw-8
//   if 6-bit  -> stream of 6-bit chars (A–Z,a–z,0–9,space=62), then terminator=63
//   if raw-8  -> stream of bytes, then terminator=0xFF
//
// OT name (always present):
//   nameMode:1              0=6-bit alphabet, 1=raw-8
//   payload encoded like Nickname, with the same terminators
//
// Met / Pokerus / Evolution:
//   metLocation:8
//   metLevel:7
//   hasPokerus:1
//     if hasPokerus=1 -> days2:2   (actual days = days2+1, i.e. 1..4; strain dropped)
//     else            -> hadPokerus:1  (marks cured via strain=1)
//   hasEvo:1
//     if hasEvo=1 -> hasEvo2:1, evo1:5, [if hasEvo2] evo2:5
//
// Finally:
//   CRC16-CCITT (init 0xFFFF, poly 0x1021) appended little-endian over all prior bytes.
//   (Bitstream is byte-aligned; any pad bits in the last data byte are zero.)

enum {
    FL_HAS_NICKNAME   = 1<<0,
    FL_HAS_TERA       = 1<<1,
    FL_HAS_DMAX       = 1<<2,
    FL_GMAX           = 1<<3,
    FL_HAS_PPUPS      = 1<<4,
    FL_SHINY_WANTED   = 1<<5,
    FL_NATURE_OVR     = 1<<6,
    FL_IS_EGG         = 1<<7,
};

static size_t pack_v1(const struct Pokemon *mon, u8 *out, size_t cap)
{
    const struct BoxPokemon *b = &mon->box;

    // Decrypt secure data, map to typed substructs by type index (0..3)
    enum { WORDS = (NUM_SUBSTRUCT_BYTES * 4) / 4 };
    u32 tmp[WORDS]; decrypt_box(b, tmp, WORDS);
    const union PokemonSubstruct *dec = (const union PokemonSubstruct *)tmp;
    const u8 *ord = sSubstructOrder[b->personality % 24];
    int idx0=-1, idx1=-1, idx2=-1, idx3=-1;
    for (int s=0;s<4;s++){ if (ord[s]==0) idx0=s; else if (ord[s]==1) idx1=s; else if (ord[s]==2) idx2=s; else if (ord[s]==3) idx3=s; }
    const struct PokemonSubstruct0 *s0 = &dec[idx0].type0;
    const struct PokemonSubstruct1 *s1 = &dec[idx1].type1;
    const struct PokemonSubstruct2 *s2 = &dec[idx2].type2;
    const struct PokemonSubstruct3 *s3 = &dec[idx3].type3;

    // Core
    const u16 species = s0->species;
    const u8  level   = mon->level;          // store level (not exp)
    const u32 pid     = b->personality;
    const u8  ability = (u8)(s3->abilityNum & 3);

    // Nature override if hiddenNatureModifier != 0
    const u8 pidNature = (u8)(pid % 25);
    const u8 realNature = (u8)((pidNature + b->hiddenNatureModifier) % 25);
    const u8 hasNatureOverride = (b->hiddenNatureModifier != 0);

    // Moves + trim trailing zeros
    u16 mv[4] = { s1->move1, s1->move2, s1->move3, s1->move4 };
    u8 count = 4; while (count > 1 && mv[count-1] == 0) count--;
    const u8 hasPPUps = (s0->ppBonuses != 0);
    u8 ppUps[4] = {
        (u8)((s0->ppBonuses >> 0) & 3),
        (u8)((s0->ppBonuses >> 2) & 3),
        (u8)((s0->ppBonuses >> 4) & 3),
        (u8)((s0->ppBonuses >> 6) & 3),
    };

    // EVs in quarters
    u8 evQ[6] = {
        (u8)(s2->hpEV >> 2), (u8)(s2->attackEV >> 2), (u8)(s2->defenseEV >> 2),
        (u8)(s2->speedEV >> 2), (u8)(s2->spAttackEV >> 2), (u8)(s2->spDefenseEV >> 2),
    };

    // IV compact
    u8 maskNon31, maskNonZero, ivVals[6], ivValsCount;
    ivs_to_masks_vals(s3, &maskNon31, &maskNonZero, ivVals, &ivValsCount);

    // Optionals
    const u8 hasTera = (s0->teraType != 0);
    const u8 teraType = (u8)(s0->teraType & 31);
    const u8 hasDmax = (s3->dynamaxLevel != 0);
    const u8 dmaxLvl = (u8)(s3->dynamaxLevel & 15);
    const u8 gmax = (s3->gigantamaxFactor ? 1 : 0);

    // Friendship scheme
    u8 friendCustom, friendFull, friendVal;
    if (s0->friendship == 0)      { friendCustom=0; friendFull=0; friendVal=0;   }
    else if (s0->friendship==255) { friendCustom=0; friendFull=1; friendVal=0;   }
    else                          { friendCustom=1; friendFull=0; friendVal=s0->friendship; }

    // Nickname (7-bit, max 10 chars)
    u8 nick12[POKEMON_NAME_LENGTH];
    u8 nickLen12 = get_nickname12(b, nick12);
    bool8 nicknameIsSpecies = BoxNicknameEqualsSpecies(b, species);
    bool8 hasNickname = (nickLen12 != 0 && !nicknameIsSpecies);

    // OT name (raw bytes, FF-terminated)
    u8 ot[PLAYER_NAME_LENGTH]; u8 otLen = get_otname(b, ot, PLAYER_NAME_LENGTH);

    // Shiny & egg flags
    const u8 shinyWanted = (b->shinyModifier != 0);
    const u8 isEgg = (u8)(s3->isEgg ? 1 : 0);

    // Pokerus fields (drop strain)
    const u8 pk = s3->pokerus;
    const u8 pkDays = (pk >> 4) & 0xF;
    const u8 pkStrain = pk & 0xF;
    const u8 hasPokerus = (pkDays > 0);
    const u8 days2 = (hasPokerus ? (u8)((pkDays-1) & 3) : 0);
    const u8 hadPokerus = (!hasPokerus && pkStrain != 0) ? 1 : 0;

    // Evolution tracker presence
    const u8 et1 = (u8)(s1->evolutionTracker1 & 31);
    const u8 et2 = (u8)(s1->evolutionTracker2 & 31);
    const u8 hasEvo = (et1 | et2) != 0;
    const u8 hasEvo2 = (et2 != 0);

    // Flags (8 bits) — unchanged layout
    u8 flags = 0;
    if (hasNickname)       flags |= FL_HAS_NICKNAME;
    if (hasTera)           flags |= FL_HAS_TERA;
    if (hasDmax)           flags |= FL_HAS_DMAX;
    if (gmax)              flags |= FL_GMAX;
    if (hasPPUps)          flags |= FL_HAS_PPUPS;
    if (shinyWanted)       flags |= FL_SHINY_WANTED;
    if (hasNatureOverride) flags |= FL_NATURE_OVR;
    if (isEgg)             flags |= FL_IS_EGG;

    // Pack
    BitW w; bw_init(&w, out, cap);
    if (!bw_put(&w, TRADE_V1, 5)) return 0;
    if (!bw_put(&w, flags, 8))    return 0;

    if (!bw_put(&w, species, 11)) return 0;
    if (!bw_put(&w, level,   7))  return 0;
    if (!bw_put(&w, pid,     32)) return 0;
    if (!bw_put(&w, b->otId, 32)) return 0;
    if (!bw_put(&w, ability, 2))  return 0;

    if (hasNatureOverride)
        if (!bw_put(&w, realNature, 5)) return 0;

    // Moves (per-species 7-bit indices with escape to 11-bit global move)
    if (!bw_put(&w, (u32)(count - 1), 2)) return 0;
    for (u32 i = 0; i < count; i++)
    {
        u8 midx = Trade_MoveToLocalIndex(species, mv[i]);
        if (midx != 0xFF) {
            if (!bw_put(&w, midx, MOVE_LOCAL_BITS)) return 0;
        } else {
            if (!bw_put(&w, MOVE_LOCAL_ESCAPE, MOVE_LOCAL_BITS)) return 0;
            if (!bw_put(&w, mv[i] & 2047u, 11)) return 0; // raw fallback
        }
    }
    if (hasPPUps)
        for (u32 i = 0; i < count; i++)
            if (!bw_put(&w, ppUps[i] & 3u, 2)) return 0;

    if (!bw_put_evs(&w, evQ)) return 0;

    if (!bw_put(&w, maskNon31 & 0x3Fu, 6)) return 0;
    if (!bw_put(&w, maskNonZero & 0x3Fu, 6)) return 0;
    for (u32 i=0;i<ivValsCount;i++) if (!bw_put(&w, ivVals[i] & 31u, 5)) return 0;

    if (hasTera) if (!bw_put(&w, teraType, 5)) return 0;
    if (hasDmax) if (!bw_put(&w, dmaxLvl, 4)) return 0;

    // Friendship payload
    if (!bw_put(&w, friendCustom ? 1u : 0u, 1)) return 0;
    if (friendCustom) {
        if (!bw_put(&w, friendVal, 8)) return 0;
    } else {
        if (!bw_put(&w, friendFull ? 1u : 0u, 1)) return 0;
    }

    // Nickname payload
    if (hasNickname) {
        u8 nickAscii[POKEMON_NAME_LENGTH];
        u8 nickLenAsc = DecodeBoxStringToAscii(nick12, nickAscii, 12);
        if (!bw_put_name6or8(&w, nickAscii, nickLenAsc)) return 0;
    }

    // OT name: raw bytes, then 0xFF terminator (always present)
    u8 otAscii[PLAYER_NAME_LENGTH];
    u8 otLenAsc = DecodeBoxStringToAscii(b->otName, otAscii, PLAYER_NAME_LENGTH);
    if (!bw_put_name6or8(&w, otAscii, otLenAsc)) return 0;

    // Met info (always present)
    if (!bw_put(&w, s3->metLocation, 8)) return 0;
    if (!bw_put(&w, s3->metLevel & 0x7F, 7)) return 0;

    // Pokerus (drop strain)
    if (!bw_put(&w, hasPokerus ? 1u : 0u, 1)) return 0;
    if (hasPokerus) {
        if (!bw_put(&w, days2 & 3u, 2)) return 0;
    } else {
        if (!bw_put(&w, hadPokerus ? 1u : 0u, 1)) return 0;
    }

    // Evolution tracker
    if (!bw_put(&w, hasEvo ? 1u : 0u, 1)) return 0;
    if (hasEvo) {
        if (!bw_put(&w, hasEvo2 ? 1u : 0u, 1)) return 0;
        if (!bw_put(&w, et1 & 31u, 5)) return 0;
        if (hasEvo2) if (!bw_put(&w, et2 & 31u, 5)) return 0;
    }

    size_t bytes = bw_bytes(&w);
    if (bytes + 2 > cap) return 0;
    u16 crc = crc16_ccitt(out, bytes);
    out[bytes++] = (u8)(crc & 0xFF);
    out[bytes++] = (u8)(crc >> 8);
    return bytes;
}

static bool8 unpack_v1_into(const u8 *in, size_t inLen, struct Pokemon *dst)
{
    if (inLen < 2) return 0;
    const u16 want = (u16)(in[inLen-2] | (in[inLen-1] << 8));
    const u16 got  = crc16_ccitt(in, inLen-2);
    if (want != got) return 0;

    BitR r; br_init(&r, in, inLen-2);
    if (br_get(&r,5) != TRADE_V1) return 0;

    const u8 flags      = (u8)br_get(&r,8);
    const u8 hasNick    = (flags & FL_HAS_NICKNAME)   != 0;
    const u8 hasTera    = (flags & FL_HAS_TERA)       != 0;
    const u8 hasDmax    = (flags & FL_HAS_DMAX)       != 0;
    const u8 gmax       = (flags & FL_GMAX)           != 0;
    const u8 hasPPUps   = (flags & FL_HAS_PPUPS)      != 0;
    const u8 shinyWant  = (flags & FL_SHINY_WANTED)   != 0;
    const u8 natOverride= (flags & FL_NATURE_OVR)     != 0;
    const u8 isEgg      = (flags & FL_IS_EGG)         != 0;

    const u16 species   = (u16)br_get(&r,11);
    const u8  level     = (u8) br_get(&r,7);
    const u32 pid       = br_get(&r,32);
    const u32 otId      = br_get(&r,32);
    const u8  ability   = (u8)br_get(&r,2);

    const u8 pidNature  = (u8)(pid % 25);
    const u8 natureVal  = natOverride ? (u8)br_get(&r,5) : pidNature;

    // Moves
    u8 count = (u8)br_get(&r, 2) + 1; if (count > 4) return 0;
    u16 mv[4] = {0,0,0,0};
    for (u32 i = 0; i < count; i++)
    {
        u8 midx = (u8)br_get(&r, MOVE_LOCAL_BITS);
        if (midx == MOVE_LOCAL_ESCAPE) {
            mv[i] = (u16)br_get(&r, 11);
        } else {
            u16 m;
            if (!Trade_LocalIndexToMove(species, midx, &m)) return 0;
            mv[i] = m;
        }
    }
    u8 ppUps[4] = {0,0,0,0};
    if (hasPPUps) for (u32 i = 0; i < count; i++) ppUps[i] = (u8)br_get(&r, 2);

    // EV quartersu8 evQ[6];
    u8 evQ[6];
    if (!br_get_evs(&r, evQ)) return 0;

    // IVs
    const u8 maskNon31   = (u8)br_get(&r,6);
    const u8 maskNonZero = (u8)br_get(&r,6);
    u8 needVals = 0; for (u32 i=0;i<6;i++) if ((maskNon31&(1u<<i))&&(maskNonZero&(1u<<i))) needVals++;
    u8 vals[6]; for (u32 i=0;i<needVals;i++) vals[i] = (u8)br_get(&r,5);
    u8 iv[6]; ivs_from_masks_vals(maskNon31, maskNonZero, vals, iv);

    // Optionals
    const u8 teraType = hasTera ? (u8)br_get(&r,5) : 0;
    const u8 dmaxLvl  = hasDmax ? (u8)br_get(&r,4) : 0;

    // Friendship payload
    u8 friendCustom = (u8)br_get(&r,1);
    u8 friendVal=0, friendFull=0;
    if (friendCustom) friendVal = (u8)br_get(&r,8);
    else              friendFull = (u8)br_get(&r,1);

    // ---- Nickname
    u8 nick12[POKEMON_NAME_LENGTH];
    u8 nickLenStored = 0;
    if (hasNick) {
        u8 nickAscii[POKEMON_NAME_LENGTH];
        u8 nickLenAsc = 0;
        if (!br_get_name6or8(&r, nickAscii, POKEMON_NAME_LENGTH, &nickLenAsc)) return 0;
        nickLenStored = EncodeAsciiToBoxString(nickAscii, nickLenAsc, nick12, POKEMON_NAME_LENGTH);
    } else {
        nickLenStored = SpeciesNameToNick12(species, nick12);
    }

    // ---- OT name (always present)
    u8 otAscii[PLAYER_NAME_LENGTH]; u8 otLenAsc = 0;
    if (!br_get_name6or8(&r, otAscii, PLAYER_NAME_LENGTH, &otLenAsc)) return 0;
    u8 otRaw[PLAYER_NAME_LENGTH];
    u8 otLenStored = EncodeAsciiToBoxString(otAscii, otLenAsc, otRaw, PLAYER_NAME_LENGTH);

    // Met info (always present)
    const u8 metLocation = (u8)br_get(&r,8);
    const u8 metLevel    = (u8)br_get(&r,7);

    // Pokerus
    const u8 hasPokerus = (u8)br_get(&r,1);
    u8 pokerusVal = 0;
    if (hasPokerus) {
        u8 days2 = (u8)br_get(&r,2);       // 0..3 => 1..4 days
        u8 days  = (u8)((days2 & 3) + 1);
        pokerusVal = (u8)((days << 4) | 1); // strain=1 (dropped on wire)
    } else {
        u8 hadPokerus = (u8)br_get(&r,1);
        pokerusVal = hadPokerus ? 0x01 : 0x00; // cured uses strain marker
    }

    // Evolution tracker
    const u8 hasEvo = (u8)br_get(&r,1);
    u8 et1=0, et2=0;
    if (hasEvo) {
        u8 hasEvo2 = (u8)br_get(&r,1);
        et1 = (u8)br_get(&r,5);
        et2 = hasEvo2 ? (u8)br_get(&r,5) : 0;
    }

    if (r.err) return 0;

    // ---- Build BoxPokemon back ----
    union PokemonSubstruct sub[4]; mem_set((u8*)sub, 0, sizeof(sub));

    // type0 (growth)
    sub[0].type0.species   = species;
    sub[0].type0.teraType  = teraType;
    sub[0].type0.heldItem  = 0;
    sub[0].type0.experience= 0; // store level instead
    sub[0].type0.ppBonuses =
        (ppUps[0] & 3) << 0 |
        (ppUps[1] & 3) << 2 |
        (ppUps[2] & 3) << 4 |
        (ppUps[3] & 3) << 6;
    sub[0].type0.friendship = friendCustom ? friendVal : (friendFull ? 255 : 0);
    sub[0].type0.pokeball   = 0;
    sub[0].type0.experience = gExperienceTables[gSpeciesInfo[species].growthRate][level];
    // nickname 10 + 2 slots (actual bytes go in BoxPokemon below)

    // type1 (moves + evo trackers)
    sub[1].type1.move1 = (count > 0) ? mv[0] : 0;
    sub[1].type1.move2 = (count > 1) ? mv[1] : 0;
    sub[1].type1.move3 = (count > 2) ? mv[2] : 0;
    sub[1].type1.move4 = (count > 3) ? mv[3] : 0;
    sub[1].type1.pp1 = (count > 0) ? CalculatePPWithBonus(mv[0], sub[0].type0.ppBonuses, 0) : 0;
    sub[1].type1.pp2 = (count > 1) ? CalculatePPWithBonus(mv[1], sub[0].type0.ppBonuses, 1) : 0;
    sub[1].type1.pp3 = (count > 2) ? CalculatePPWithBonus(mv[2], sub[0].type0.ppBonuses, 2) : 0;
    sub[1].type1.pp4 = (count > 3) ? CalculatePPWithBonus(mv[3], sub[0].type0.ppBonuses, 3) : 0;

    sub[1].type1.evolutionTracker1 = et1 & 31;
    sub[1].type1.evolutionTracker2 = et2 & 31;

    // type2 (EVs — multiply by 4)
    sub[2].type2.hpEV       = (u8)(evQ[0] << 2);
    sub[2].type2.attackEV   = (u8)(evQ[1] << 2);
    sub[2].type2.defenseEV  = (u8)(evQ[2] << 2);
    sub[2].type2.speedEV    = (u8)(evQ[3] << 2);
    sub[2].type2.spAttackEV = (u8)(evQ[4] << 2);
    sub[2].type2.spDefenseEV= (u8)(evQ[5] << 2);

    // type3 (IVs/misc)
    sub[3].type3.hpIV        = iv[0];
    sub[3].type3.attackIV    = iv[1];
    sub[3].type3.defenseIV   = iv[2];
    sub[3].type3.speedIV     = iv[3];
    sub[3].type3.spAttackIV  = iv[4];
    sub[3].type3.spDefenseIV = iv[5];
    sub[3].type3.isEgg       = (flags & FL_IS_EGG) ? 1u : 0u;
    sub[3].type3.gigantamaxFactor = (flags & FL_GMAX) ? 1u : 0u;
    sub[3].type3.dynamaxLevel     = dmaxLvl;
    sub[3].type3.abilityNum       = ability;
    sub[3].type3.pokerus          = pokerusVal;
    sub[3].type3.metLocation      = metLocation;
    sub[3].type3.metLevel         = metLevel & 0x7F;

    // Install into BoxPokemon (encrypt after ordering)
    struct BoxPokemon *b = &dst->box;
    mem_set((u8*)b, 0, sizeof(*b));
    b->personality = pid;
    b->otId        = otId;
    b->hasSpecies  = 1;
    b->hiddenNatureModifier = (u8)((natureVal + 25 - (pid % 25)) % 25);
    b->shinyModifier = shinyWant ? 1 : 0;

    // nickname
    ApplyNick12ToBox(b, &sub[0].type0, nick12, nickLenStored);
    

    // OT name
    for (int i = 0; i < PLAYER_NAME_LENGTH; i++) b->otName[i] = (i < otLenStored) ? otRaw[i] : 0xFF;

    // Place in secure order, checksum, encrypt
    union PokemonSubstruct enc[4];
    const u8 *ord = sSubstructOrder[b->personality % 24];
    for (int slot=0; slot<4; slot++) enc[slot] = sub[ord[slot]];
    b->checksum = calc_secure_checksum(enc);

    enum { WORDS2 = (NUM_SUBSTRUCT_BYTES * 4) / 4 };
    encrypt_box(b, (const u32 *)enc, WORDS2);
    CalculateMonStats(dst);

    // Party fields: carry level (others left for normal init paths)
    dst->level = level;
    dst->hp = dst->maxHP;
    dst->mail = 255;
    return 1;
}


// ---------------- public API ----------------
size_t TradeMon_Encode(const struct Pokemon *mon, char *outStr, size_t outCap)
{
    u8 buf[96];
    size_t binLen = pack_v1(mon, buf, sizeof(buf));
    if (!binLen) return 0;
    return b64url_encode(buf, binLen, outStr, outCap ? outCap-1 : 0);
}

bool8 TradeMon_Decode(const char *str, struct Pokemon *dst)
{
    u8 buf[96];
    size_t n = b64url_decode(str, buf, sizeof(buf));
    if (!n) return 0;
    return unpack_v1_into(buf, n, dst);
}

void MgbaDumpPokemon(int level, const struct Pokemon *p)
{
    if (!p)
    {
        MgbaPrintf(level, "MgbaDumpPokemon: NULL pointer");
        return;
    }

    MgbaPrintf(level, "Pokemon {");
    DumpBoxPokemon(level, &p->box);

    MgbaPrintf(level, "  status:   0x%08X", (unsigned)p->status);
    MgbaPrintf(level, "  level:    %u", p->level);
    MgbaPrintf(level, "  mail:     %u", p->mail);

    MgbaPrintf(level, "  hp:       %u", p->hp);
    MgbaPrintf(level, "  maxHP:    %u", p->maxHP);
    MgbaPrintf(level, "  attack:   %u", p->attack);
    MgbaPrintf(level, "  defense:  %u", p->defense);
    MgbaPrintf(level, "  speed:    %u", p->speed);
    MgbaPrintf(level, "  spAttack: %u", p->spAttack);
    MgbaPrintf(level, "  spDefense:%u", p->spDefense);
    MgbaPrintf(level, "}");
    
    DumpHexU8(level, "Pokemon [raw struct bytes]", (const u8 *)p, sizeof(*p));

    char tradeStr[192];
    size_t tradeStrLen = TradeMon_Encode(p, tradeStr, sizeof(tradeStr));
    if (!tradeStrLen)
    {
        MgbaPrintf(level, "Trade encode FAILED");
        return;
    }
    MgbaPrintf(level, "Trade (v1) string (%u chars):", (unsigned)tradeStrLen);
    MgbaPrintf(level, "  %s", tradeStr);

    // ---- Show raw payload bytes (decode base64url back to bytes) ----
    u8 payload[96];
    size_t payloadLen = b64url_decode(tradeStr, payload, sizeof(payload));
    if (!payloadLen)
    {
        MgbaPrintf(level, "Trade payload decode FAILED");
        return;
    }
    DumpHexU8(level, "Trade (v1) payload [CRC16 appended]", payload, payloadLen);

    // ---- Rebuild from trade string and dump again ----
    struct Pokemon rebuilt;
    mem_set((u8 *)&rebuilt, 0, sizeof(rebuilt));

    if (!TradeMon_Decode(tradeStr, &rebuilt))
    {
        MgbaPrintf(level, "Trade decode FAILED");
        return;
    }

    MgbaPrintf(level, "Rebuilt Pokemon {");
    DumpBoxPokemon(level, &rebuilt.box);

    MgbaPrintf(level, "  status:   0x%08X", (unsigned)rebuilt.status);
    MgbaPrintf(level, "  level:    %u", rebuilt.level);
    MgbaPrintf(level, "  mail:     %u", rebuilt.mail);

    MgbaPrintf(level, "  hp:       %u", rebuilt.hp);
    MgbaPrintf(level, "  maxHP:    %u", rebuilt.maxHP);
    MgbaPrintf(level, "  attack:   %u", rebuilt.attack);
    MgbaPrintf(level, "  defense:  %u", rebuilt.defense);
    MgbaPrintf(level, "  speed:    %u", rebuilt.speed);
    MgbaPrintf(level, "  spAttack: %u", rebuilt.spAttack);
    MgbaPrintf(level, "  spDefense:%u", rebuilt.spDefense);
    MgbaPrintf(level, "}");
    DumpHexU8(level, "Rebuilt [raw struct bytes]", (const u8 *)&rebuilt, sizeof(rebuilt));
}
#endif