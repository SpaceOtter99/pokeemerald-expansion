#include "global.h"
#include "pokemon.h"
// #include "isagbprint.h"
#include "mini_printf.h"
#include "_debug_utils.h"

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

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


static const u8 sSubstructOrder[24][4] = {
    {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},
    {1,0,2,3},{1,0,3,2},{2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},
    {1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},{2,3,0,1},{3,2,0,1},
    {1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0},
};

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

// Decrypt and print typed substructs
static void DumpDecryptedSubstructs(int level, const struct BoxPokemon *b)
{
    // encryption key
    u32 key = b->personality ^ b->otId;

    // Decrypt 4 * NUM_SUBSTRUCT_BYTES bytes (usually 48B) into a local buffer of u32s
    enum { WORDS = (NUM_SUBSTRUCT_BYTES * 4) / 4 };
    u32 tmp[WORDS];
    const u32 *src = (const u32 *)b->secure.raw;
    for (u32 i = 0; i < WORDS; i++)
        tmp[i] = src[i] ^ key;

    // Interpret decrypted bytes as 4 union PokemonSubstruct
    const union PokemonSubstruct *dec = (const union PokemonSubstruct *)tmp;

    // Determine which slot holds each type
    const u8 *ord = sSubstructOrder[b->personality % 24];
    int idxOf[4] = {-1,-1,-1,-1};
    for (int slot = 0; slot < 4; slot++)
        idxOf[ord[slot]] = slot;  // e.g. idxOf[0] = slot holding type0

    MgbaPrintf(level, "  secure (decrypted) { key=0x%08X, order=[%u,%u,%u,%u] }",
               (unsigned)key, ord[0], ord[1], ord[2], ord[3]);

    // Now dump each typed view using its slot
    DumpSubstruct0(level, &dec[idxOf[0]].type0);
    DumpSubstruct1(level, &dec[idxOf[1]].type1);
    DumpSubstruct2(level, &dec[idxOf[2]].type2);
    DumpSubstruct3(level, &dec[idxOf[3]].type3);
}

static void DumpBoxPokemon(int level, const struct BoxPokemon *b)
{
    MgbaPrintf(level, "BoxPokemon {");
    MgbaPrintf(level, "  personality: 0x%08X", (unsigned)b->personality);
    MgbaPrintf(level, "  otId:        0x%08X", (unsigned)b->otId);

    DumpHexU8(level, "  nickname", b->nickname, ARRAY_COUNT(b->nickname));

    MgbaPrintf(level, "  language: %u", b->language);
    MgbaPrintf(level, "  hiddenNatureModifier: %u", b->hiddenNatureModifier);

    MgbaPrintf(level, "  isBadEgg: %u", b->isBadEgg);
    MgbaPrintf(level, "  hasSpecies: %u", b->hasSpecies);
    MgbaPrintf(level, "  isEgg: %u", b->isEgg);
    MgbaPrintf(level, "  blockBoxRS: %u", b->blockBoxRS);
    MgbaPrintf(level, "  daysSinceFormChange: %u", b->daysSinceFormChange);
    MgbaPrintf(level, "  unused_13: %u", b->unused_13);

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
}