#!/usr/bin/env python3
import re
import sys
from pathlib import Path
from typing import Dict, Tuple, List, Set

# --------- REGEXES (robust to whitespace/newlines) ---------
LEVELUP_RE = re.compile(
    r"static\s+const\s+struct\s+LevelUpMove\s+"
    r"(s(?P<name>[A-Za-z0-9_]+)LevelUpLearnset)\s*\[\]\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL
)

TEACHABLE_RE = re.compile(
    r"static\s+const\s+u16\s+"
    r"(s(?P<name>[A-Za-z0-9_]+)TeachableLearnset)\s*\[\]\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL
)

EGG_RE = re.compile(
    r"static\s+const\s+u16\s+"
    r"(s(?P<name>[A-Za-z0-9_]+)EggMoveLearnset)\s*\[\]\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL
)

# --------- HELPERS ---------
def strip_comments(s: str) -> str:
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.DOTALL)   # /* ... */
    s = re.sub(r"//.*?$", "", s, flags=re.MULTILINE)   # // ...
    return s

def strip_move_prefix(mv: str) -> str:
    # Return bare move name (e.g., "TACKLE" from "MOVE_TACKLE")
    return mv[5:] if mv.startswith("MOVE_") else mv

# --------- PARSERS THAT RETURN MOVE SETS ---------
def parse_levelup_moves(text: str) -> Dict[str, Set[str]]:
    """
    Returns {PokemonName: {move_names}} for all level-up learnsets.
    Extracts the MOVE_* token as the second argument of LEVEL_UP_MOVE(...).
    """
    result: Dict[str, Set[str]] = {}
    for m in LEVELUP_RE.finditer(text):
        name = m.group("name")
        body = strip_comments(m.group("body"))
        # Capture second argument (MOVE_*) inside LEVEL_UP_MOVE(level, MOVE_XXX)
        moves = re.findall(r"\bLEVEL_UP_MOVE\s*\(\s*[^,]+,\s*(MOVE_[A-Z0-9_]+)\s*\)", body)
        mvset = {strip_move_prefix(x) for x in moves if x not in {"MOVE_UNAVAILABLE", "MOVE_NONE"}}
        # Keep the superset if seen multiple times
        result[name] = result.get(name, set()) | mvset
    return result

def parse_move_array_moves(text: str, regex: re.Pattern) -> Dict[str, Set[str]]:
    """
    Shared logic for Teachable/Egg arrays: collects MOVE_* entries (excluding sentinels).
    """
    result: Dict[str, Set[str]] = {}
    for m in regex.finditer(text):
        name = m.group("name")
        body = strip_comments(m.group("body"))
        moves = re.findall(r"\bMOVE_[A-Z0-9_]+\b", body)
        mvset = {strip_move_prefix(x) for x in moves if x not in {"MOVE_UNAVAILABLE", "MOVE_NONE"}}
        result[name] = result.get(name, set()) | mvset
    return result

def load_text(path: str) -> str:
    return Path(path).read_text(encoding="utf-8", errors="ignore")

# --------- CORE ---------
def aggregate_moves(
    levelup_file: str,
    teachable_file: str,
    egg_file: str
) -> Tuple[Dict[str, Set[str]], Dict[str, Set[str]], Dict[str, Set[str]], Dict[str, Set[str]]]:
    """
    Returns:
      levelup_moves, teachable_moves, egg_moves, dedup_all_moves
    where dedup_all_moves[name] = union(levelup, teachable, egg) with duplicates removed.
    """
    levelup_moves = parse_levelup_moves(load_text(levelup_file))
    teachable_moves = parse_move_array_moves(load_text(teachable_file), TEACHABLE_RE)
    egg_moves = parse_move_array_moves(load_text(egg_file), EGG_RE)

    names = set(levelup_moves) | set(teachable_moves) | set(egg_moves)

    dedup_all: Dict[str, Set[str]] = {}
    for n in names:
        lu = levelup_moves.get(n, set())
        te = teachable_moves.get(n, set())
        eg = egg_moves.get(n, set())
        dedup_all[n] = lu | te | eg

    return levelup_moves, teachable_moves, egg_moves, dedup_all

def find_max_totals(dedup_all: Dict[str, Set[str]]) -> List[Tuple[str, int]]:
    """
    Finds the Pokémon with the largest number of UNIQUE moves across all sources.
    Returns a list of (name, unique_total), sorted by name.
    """
    if not dedup_all:
        return []
    max_total = max(len(v) for v in dedup_all.values())
    winners = [(k, len(v)) for k, v in dedup_all.items() if len(v) == max_total]
    winners.sort(key=lambda x: x[0].lower())
    return winners

# --------- CLI ---------
def main(argv: List[str]) -> None:
    if len(argv) != 4:
        print("Usage: python master_learnset_max.py <levelup.c> <teachable.c> <egg.c>")
        sys.exit(1)

    levelup_file, teachable_file, egg_file = argv[1], argv[2], argv[3]

    levelup_moves, teachable_moves, egg_moves, dedup_all = aggregate_moves(levelup_file, teachable_file, egg_file)
    winners = find_max_totals(dedup_all)

    if not winners:
        print("No learnsets found across the provided files.")
        return

    print("== Biggest Combined Learnset(s) by UNIQUE moves ==")
    for name, unique_total in winners:
        lu = levelup_moves.get(name, set())
        te = teachable_moves.get(name, set())
        eg = egg_moves.get(name, set())
        all_moves = sorted(dedup_all[name])
        print(f"{name}: unique_total={unique_total} (level-up={len(lu)}, teachable={len(te)}, egg={len(eg)})")
        print("  Unique moves (deduplicated):")
        # Soft wrap: print in comma-separated lines for readability
        line = []
        width = 0
        for mv in all_moves:
            token = mv
            if width + len(token) + (2 if line else 0) > 100:
                print("   - " + ", ".join(line))
                line, width = [], 0
            line.append(token)
            width += len(token) + (2 if line else 0)
        if line:
            print("   - " + ", ".join(line))

    # If you also want the single top entry only:
    # top_name, unique_total = winners[0]
    # print(f"Winner -> {top_name}: unique_total={unique_total}")

if __name__ == "__main__":
    main(sys.argv)
