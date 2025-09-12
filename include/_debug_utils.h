#pragma once

struct Pokemon;

#ifndef NDEBUG
void MgbaDumpPokemon(int level, const struct Pokemon *p);
#else
#define MgbaDumpPokemon(...)
#endif
