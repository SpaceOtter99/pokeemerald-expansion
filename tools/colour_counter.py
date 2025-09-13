#!/usr/bin/env python3
"""
pick_pokemon_colors_from_anim.py

Hatchable base-form detection (families files only):
- Include species iff:
  (a) it has at least one valid egg group (not UNDISCOVERED/NO_EGGS/NONE), and
  (b) it does NOT appear as a target in any `.evolutions = { ... }` / EVOLUTION({ ... }) block
  from src/data/pokemon/species_info/*families.h.

Colour selection (anim_front.png + back.png):
- Exclude palette index 0 and fully transparent pixels from counting.
- If FRONT is indexed ('P'): use its palette (and tRNS) for BOTH front & back; if back is RGBA,
  map each pixel to the NEAREST front-palette colour.
- If FRONT is not indexed: try to load species palette from files
  (graphics/pokemon/<species>/normal.gbapal or normal.pal) and map BOTH images to that palette.
- If no palette can be obtained, fall back to RGBA counts (alpha>0).
- Primary = max LCH chroma within L* bounds (default 20–90) that covers at least min-body-frac
  (default 0.25) of combined body (with 1% backoff).
- Secondary = next most common eligible colour (within L* bounds) with hue ≥ hue-sep-deg
  (default 12°) from primary.
- Percentages shown are **band-aggregated coverage**:
  - For each picked colour, sum all hues within ±hue-sep-deg of that colour,
    **ignoring L* bounds**. Secondary band excludes hues already counted in primary.

Requires: Pillow (pip install pillow)
"""

import argparse
import math
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

try:
    from PIL import Image
except ImportError:
    print("ERROR: This script requires Pillow. Install it with: pip install pillow", file=sys.stderr)
    sys.exit(1)

# ---------- Color conversions (sRGB D65 -> Lab -> LCH) ----------

def _srgb_to_linear(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def _rgb8_to_xyz(r: int, g: int, b: int) -> Tuple[float, float, float]:
    R, G, B = (_srgb_to_linear(r/255.0), _srgb_to_linear(g/255.0), _srgb_to_linear(b/255.0))
    X = 0.4124564 * R + 0.3575761 * G + 0.1804375 * B
    Y = 0.2126729 * R + 0.7151522 * G + 0.0721750 * B
    Z = 0.0193339 * R + 0.1191920 * G + 0.9503041 * B
    return X, Y, Z

_Xn, _Yn, _Zn = (0.95047, 1.00000, 1.08883)

def _f_lab(t: float) -> float:
    d = 6/29
    return t ** (1/3) if t > d**3 else (t / (3 * d * d) + 4/29)

def _xyz_to_lab(X: float, Y: float, Z: float) -> Tuple[float, float, float]:
    fx, fy, fz = _f_lab(X/_Xn), _f_lab(Y/_Yn), _f_lab(Z/_Zn)
    L = 116*fy - 16
    a = 500*(fx - fy)
    b = 200*(fy - fz)
    return L, a, b

def _lab_to_lch(L: float, a: float, b: float) -> Tuple[float, float, float]:
    C = math.hypot(a, b)
    h = math.degrees(math.atan2(b, a)) % 360.0
    return L, C, h

def rgb8_to_lch(r: int, g: int, b: int) -> Tuple[float, float, float]:
    X, Y, Z = _rgb8_to_xyz(r, g, b)
    L, a, bb = _xyz_to_lab(X, Y, Z)
    return _lab_to_lch(L, a, bb)

def hex_from_rgb(rgb: Tuple[int, int, int]) -> str:
    return "#{:02X}{:02X}{:02X}".format(*rgb)

def ansi_block(rgb: Tuple[int,int,int]) -> str:
    r, g, b = rgb
    return f"\x1b[48;2;{r};{g};{b}m  \x1b[0m"

def _hue_deg(rgb: Tuple[int,int,int]) -> float:
    return rgb8_to_lch(*rgb)[2]

def _hue_dist(a: float, b: float) -> float:
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)

# ---------- Repo helpers ----------

def iter_species_dirs(root: Path) -> List[Path]:
    base = root / "graphics" / "pokemon"
    return [p for p in sorted(base.iterdir()) if p.is_dir()] if base.is_dir() else []

def species_dir_to_macro_name(species_dir_name: str) -> str:
    token = re.sub(r"[^A-Za-z0-9]+", "_", species_dir_name).strip("_")
    return token.upper()

# ---------- Families parsing (egg groups + evolution targets) ----------

_UNHATCHABLE_EGG_TOKENS = {"EGG_GROUP_UNDISCOVERED", "EGG_GROUP_NO_EGGS", "EGG_GROUP_NONE"}

def load_family_files(root: Path) -> List[Path]:
    d = root / "src" / "data" / "pokemon" / "species_info"
    return [p for p in d.glob("*families.h") if p.is_file()]

def parse_families(root: Path) -> Tuple[Dict[str, Set[str]], Set[str]]:
    species_egg_groups: Dict[str, Set[str]] = {}
    evolution_targets: Set[str] = set()

    files = load_family_files(root)
    if not files:
        return species_egg_groups, evolution_targets

    species_block_re = re.compile(r"\[\s*SPECIES_([A-Z0-9_]+)\s*\]\s*=\s*\{", re.MULTILINE)
    egg_groups_line_re = re.compile(r"\.eggGroups\s*=\s*MON_EGG_GROUPS\s*\(([^)]+)\)")
    evol_block_re = re.compile(r"\.\s*evolutions\s*=\s*EVOLUTION\s*\(\s*\{(.*?)\}\s*\)", re.DOTALL)
    evol_block_alt_re = re.compile(r"\.\s*evolutions\s*=\s*\{\s*(.*?)\s*\}", re.DOTALL)
    species_token_re = re.compile(r"\bSPECIES_([A-Z0-9_]+)\b")
    egg_token_re = re.compile(r"\bEGG_GROUP_[A-Z0-9_]+\b")

    for path in files:
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue

        for m in species_block_re.finditer(text):
            start = m.end()
            depth, i = 1, start
            while i < len(text) and depth > 0:
                if text[i] == "{": depth += 1
                elif text[i] == "}": depth -= 1
                i += 1
            block = text[start:i-1] if depth == 0 else ""
            species_tok = m.group(1)

            em = egg_groups_line_re.search(block)
            if em:
                inside = em.group(1)
                eggs = set(t.strip() for t in egg_token_re.findall(inside))
                if eggs:
                    species_egg_groups[species_tok] = eggs

            for evol_re in (evol_block_re, evol_block_alt_re):
                for b in evol_re.findall(block):
                    for sm in species_token_re.finditer(b):
                        evolution_targets.add(sm.group(1))

        for b in evol_block_alt_re.findall(text):
            for sm in species_token_re.finditer(b):
                evolution_targets.add(sm.group(1))
        for b in evol_block_re.findall(text):
            for sm in species_token_re.finditer(b):
                evolution_targets.add(sm.group(1))

    return species_egg_groups, evolution_targets

def is_hatchable_base(species_token: str, species_egg_groups: Dict[str, Set[str]], evolution_targets: Set[str]) -> bool:
    eggs = species_egg_groups.get(species_token, set())
    if not eggs:
        return False
    if all(e in _UNHATCHABLE_EGG_TOKENS for e in eggs):
        return False
    if species_token in evolution_targets:
        return False
    return True

# ---------- Palette file loaders (normal.gbapal / normal.pal) ----------

def _bgr555_to_rgb8(word: int) -> Tuple[int,int,int]:
    r5 =  word        & 0x1F
    g5 = (word >> 5)  & 0x1F
    b5 = (word >> 10) & 0x1F
    r = (r5 * 255 + 15) // 31
    g = (g5 * 255 + 15) // 31
    b = (b5 * 255 + 15) // 31
    return (r, g, b)

def _parse_gbapal_bytes(data: bytes) -> List[Tuple[int,int,int]]:
    cols = []
    for i in range(0, min(len(data), 32), 2):  # first 16
        word = int.from_bytes(data[i:i+2], "little")
        cols.append(_bgr555_to_rgb8(word))
    return cols

def _parse_gimp_pal(text: str) -> List[Tuple[int,int,int]]:
    out = []
    for ln in text.splitlines():
        ln = ln.strip()
        if not ln or ln.startswith("#") or any(ln.startswith(h) for h in ("GIMP Palette","Name:","Columns:","JASC-PAL","0100","JASC-Palette")):
            continue
        m = re.match(r"^\s*(\d{1,3})\s+(\d{1,3})\s+(\d{1,3})", ln)
        if m:
            r,g,b = map(int, m.groups())
            if 0<=r<=255 and 0<=g<=255 and 0<=b<=255:
                out.append((r,g,b))
                if len(out) == 16: break
    return out

def load_species_palette_from_files(species_dir: Path) -> Optional[List[Tuple[int,int,int]]]:
    # try normal.gbapal then normal.pal
    gbapal = species_dir / "normal.gbapal"
    pal    = species_dir / "normal.pal"
    if gbapal.exists():
        try:
            return _parse_gbapal_bytes(gbapal.read_bytes())
        except Exception:
            pass
    if pal.exists():
        try:
            return _parse_gimp_pal(pal.read_text(encoding="utf-8", errors="ignore"))
        except Exception:
            pass
    return None

# ---------- Counting & mapping to a palette ----------

def _palette_info_from_indexed(img: Image.Image) -> Tuple[List[Tuple[int,int,int]], List[int]]:
    pal = img.getpalette()
    pal_rgb = [(pal[i], pal[i+1], pal[i+2]) for i in range(0, min(len(pal), 256*3), 3)]
    trns = img.info.get("transparency", None)
    alpha = [255] * 256
    if isinstance(trns, bytes):
        for i in range(min(len(trns), 256)):
            alpha[i] = trns[i]
    elif isinstance(trns, int):
        if 0 <= trns < 256: alpha[trns] = 0
    return pal_rgb, alpha

def _count_from_indexed_using_palette_indices(img_path: Path, pal_rgb: List[Tuple[int,int,int]], pal_alpha: List[int]) -> Tuple[Counter, int]:
    im = Image.open(img_path)
    try:
        if im.mode != "P": return Counter(), 0
        pix = im.load()
        w,h = im.size
        counts = Counter(); visible = 0
        max_idx = len(pal_rgb)
        for y in range(h):
            for x in range(w):
                idx = pix[x,y]
                if idx == 0 or idx >= max_idx or pal_alpha[idx] == 0: continue
                rgb = pal_rgb[idx]
                counts[rgb] += 1; visible += 1
        return counts, visible
    finally:
        im.close()

def _count_from_rgba_to_nearest_palette(img_path: Path, pal_rgb: List[Tuple[int,int,int]], pal_alpha: Optional[List[int]]=None) -> Tuple[Counter, int]:
    # Map each visible RGBA pixel to nearest palette entry (exclude idx 0 and alpha==0 entries if pal_alpha provided)
    if pal_alpha is None: pal_alpha = [255]*len(pal_rgb)
    allowed = [(i,c) for i,c in enumerate(pal_rgb) if i != 0 and pal_alpha[i] != 0]
    if not allowed: return Counter(), 0
    im = Image.open(img_path).convert("RGBA")
    try:
        pix = im.load(); w,h = im.size
        counts = Counter(); visible = 0
        for y in range(h):
            for x in range(w):
                r,g,b,a = pix[x,y]
                if a == 0: continue
                # nearest by squared Euclidean distance
                best_i = None; best_d = 10**9
                for i,(cr,cg,cb) in allowed:
                    dr = r-cr; dg = g-cg; db = b-cb
                    d = dr*dr + dg*dg + db*db
                    if d < best_d:
                        best_d = d; best_i = i
                rgb = pal_rgb[best_i]
                counts[rgb] += 1; visible += 1
        return counts, visible
    finally:
        im.close()

def count_front_and_back_with_palette(front_path: Path, back_path: Path, species_dir: Path) -> Tuple[Counter, int]:
    """
    Combined counting with palette rules:
      - If FRONT is indexed: use its palette (and tRNS) for both.
      - Else if a palette file exists: load it and map BOTH images to nearest palette entry.
      - Else: RGBA fallback for both.
    """
    counts_total = Counter(); visible_total = 0
    if front_path.exists():
        im = Image.open(front_path)
        try:
            if im.mode == "P":
                # Use front's own palette
                pal_rgb, pal_alpha = _palette_info_from_indexed(im)
                # Front via indices
                c1, v1 = _count_from_indexed_using_palette_indices(front_path, pal_rgb, pal_alpha)
                counts_total.update(c1); visible_total += v1
                # Back: indexed -> indices through front pal; else -> RGBA to nearest front pal
                if back_path.exists():
                    back = Image.open(back_path)
                    try:
                        if back.mode == "P":
                            c2, v2 = _count_from_indexed_using_palette_indices(back_path, pal_rgb, pal_alpha)
                        else:
                            c2, v2 = _count_from_rgba_to_nearest_palette(back_path, pal_rgb, pal_alpha)
                    finally:
                        back.close()
                    counts_total.update(c2); visible_total += v2
                return counts_total, visible_total
            else:
                # Front not indexed: try species palette files
                pal = load_species_palette_from_files(species_dir)
                if pal:
                    pal_alpha = [255]*len(pal)  # assume fully opaque; treat index 0 as excluded
                    # Front & back mapped to nearest palette entries
                    c1, v1 = _count_from_rgba_to_nearest_palette(front_path, pal, pal_alpha)
                    counts_total.update(c1); visible_total += v1
                    if back_path.exists():
                        back = Image.open(back_path)
                        try:
                            if back.mode == "P":
                                # Map back indices through the loaded palette by index if possible
                                # but palettes may not match; safer to map RGBA to nearest palette
                                c2, v2 = _count_from_rgba_to_nearest_palette(back_path, pal, pal_alpha)
                            else:
                                c2, v2 = _count_from_rgba_to_nearest_palette(back_path, pal, pal_alpha)
                        finally:
                            back.close()
                    else:
                        c2, v2 = Counter(), 0
                    counts_total.update(c2); visible_total += v2
                    return counts_total, visible_total
                else:
                    # No palette available: RGBA fallback
                    c1, v1 = _count_rgba_visible(front_path)
                    counts_total.update(c1); visible_total += v1
                    if back_path.exists():
                        c2, v2 = _count_rgba_visible(back_path)
                        counts_total.update(c2); visible_total += v2
                    return counts_total, visible_total
        finally:
            im.close()
    # No front; last resort: RGBA on back
    if back_path.exists():
        c2, v2 = _count_rgba_visible(back_path)
        counts_total.update(c2); visible_total += v2
    return counts_total, visible_total

def _count_rgba_visible(img_path: Path) -> Tuple[Counter, int]:
    im = Image.open(img_path).convert("RGBA")
    try:
        pix = im.load(); w,h = im.size
        counts = Counter(); visible = 0
        for y in range(h):
            for x in range(w):
                r,g,b,a = pix[x,y]
                if a == 0: continue
                counts[(r,g,b)] += 1; visible += 1
        return counts, visible
    finally:
        im.close()

# ---------- Selection (primary with coverage; secondary not a shade) ----------

def most_chromatic_with_min_fraction(
    counts: Counter,
    visible: int,
    Lmin: float,
    Lmax: float,
    min_body_frac: float
) -> Optional[Tuple[
    Tuple[int,int,int], Tuple[float,float,float], int, float,
    List[Tuple[Tuple[int,int,int], Tuple[float,float,float], int, float]],
    List[Tuple[Tuple[int,int,int], Tuple[float,float,float], int, float]]
]]:
    """
    Build two lists:
      - eligible_l: colours within L* bounds (for primary/secondary selection)
      - all_colors: all counted colours regardless of L* (for band coverage)
    Returns primary pick + both lists.
    """
    if visible == 0 or not counts:
        return None

    eligible_l = []
    all_colors = []
    for rgb, cnt in counts.items():
        L, C, hdeg = rgb8_to_lch(*rgb)
        frac = cnt / visible
        all_colors.append((rgb, (L, C, hdeg), cnt, frac))
        if Lmin <= L <= Lmax:
            eligible_l.append((rgb, (L, C, hdeg), cnt, frac))

    if not eligible_l:
        return None

    # Primary with threshold/backoff
    thresh = min(max(min_body_frac, 0.0), 1.0)
    best = None
    while thresh >= -1e-9:
        best = None
        for idx, (rgb, lch, cnt, frac) in enumerate(eligible_l):
            if frac + 1e-12 < thresh:
                continue
            _, C, _ = lch
            key = (C, cnt, -idx)
            if (best is None) or (key > best[0]):
                best = (key, rgb, lch, cnt, frac)
        if best is not None:
            break
        thresh -= 0.01
    if best is None:
        return None

    _, p_rgb, p_lch, p_cnt, p_frac = best
    return p_rgb, p_lch, p_cnt, p_frac, eligible_l, all_colors

def pick_second_color(
    eligible_l: List[Tuple[Tuple[int,int,int], Tuple[float,float,float], int, float]],
    primary_rgb: Tuple[int,int,int],
    hue_sep_deg: float
) -> Optional[Tuple[Tuple[int,int,int], Tuple[float,float,float], int, float]]:
    p_h = _hue_deg(primary_rgb)
    cands = []
    for idx, (rgb, lch, cnt, frac) in enumerate(eligible_l):
        if rgb == primary_rgb: continue
        _, C, h = lch
        if _hue_dist(h, p_h) < hue_sep_deg: continue
        cands.append((cnt, C, -idx, rgb, lch, frac))
    if not cands: return None
    cands.sort(reverse=True)
    _, _, _, rgb, lch, frac = cands[0]
    # lookup actual cnt
    for (r2, l2, cnt2, f2) in eligible_l:
        if r2 == rgb: return rgb, lch, cnt2, f2
    return None

# ---------- Band-aggregated coverage (ignoring L* bounds) ----------

def band_coverage_fraction(
    all_colors: List[Tuple[Tuple[int,int,int], Tuple[float,float,float], int, float]],
    center_rgb: Tuple[int,int,int],
    visible: int,
    hue_sep_deg: float,
    exclude_set: Optional[Set[Tuple[int,int,int]]] = None
) -> Tuple[float, Set[Tuple[int,int,int]]]:
    if visible == 0: return 0.0, set()
    center_h = _hue_deg(center_rgb)
    total = 0; covered = set()
    for (rgb, lch, cnt, frac) in all_colors:
        if exclude_set and rgb in exclude_set: continue
        _, _, h = lch
        if _hue_dist(h, center_h) <= hue_sep_deg + 1e-12:
            total += cnt; covered.add(rgb)
    return (total / visible), covered

# ---------- Main ----------

def main():
    ap = argparse.ArgumentParser(description="Pick two body colours (band coverage ignores L*; back counts via front/loaded palette) for hatchable base-form Pokémon.")
    ap.add_argument("--root", type=str, default="pokeemerald-expansion", help="Path to repo root")
    ap.add_argument("--Lmin", type=float, default=20.0, help="Minimum CIE L* (default 20)")
    ap.add_argument("--Lmax", type=float, default=90.0, help="Maximum CIE L* (default 90)")
    ap.add_argument("--min-body-frac", type=float, default=0.25, help="Minimum combined body coverage for primary (0..1)")
    ap.add_argument("--hue-sep-deg", type=float, default=12.0, help="Hue separation for secondary & band coverage (degrees)")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    if not root.exists():
        print(f"ERROR: Root path not found: {root}", file=sys.stderr)
        sys.exit(1)

    species_egg_groups, evolution_targets = parse_families(root)

    species_dirs = iter_species_dirs(root)
    if not species_dirs:
        print("ERROR: No species directories under graphics/pokemon/.", file=sys.stderr)
        sys.exit(1)

    name_width = max((len(p.name) for p in species_dirs), default=0)
    use_ansi = sys.stdout.isatty()

    for species_dir in species_dirs:
        species = species_dir.name
        token = species_dir_to_macro_name(species)
        if not is_hatchable_base(token, species_egg_groups, evolution_targets):
            continue

        front_path = species_dir / "anim_front.png"
        back_path  = species_dir / "back.png"

        counts, visible = count_front_and_back_with_palette(front_path, back_path, species_dir)
        if visible == 0 or not counts:
            continue

        res = most_chromatic_with_min_fraction(
            counts, visible, args.Lmin, args.Lmax, args.min_body_frac
        )
        if not res:
            continue

        p_rgb, p_lch, p_cnt, p_frac, eligible_l, all_colors = res
        s = pick_second_color(eligible_l, p_rgb, hue_sep_deg=args.hue_sep_deb if hasattr(args,'hue_sep_deb') else args.hue_sep_deg)

        # Band coverage (ignoring L*), with secondary excluding primary band
        p_frac_band, p_covered = band_coverage_fraction(all_colors, p_rgb, visible, args.hue_sep_deg)
        s_frac_band = None
        if s:
            s_rgb, s_lch, s_cnt, s_frac = s
            s_frac_band, _ = band_coverage_fraction(all_colors, s_rgb, visible, args.hue_sep_deg, exclude_set=p_covered)

        # Output
        left = f"{species.ljust(name_width)}: "
        p_hex = hex_from_rgb(p_rgb)
        p_block = ansi_block(p_rgb) if use_ansi else ""
        line = f"{left}{p_block} {p_hex} ({p_frac_band*100:.1f}%)"

        if s:
            s_rgb, _, _, _ = s
            s_hex = hex_from_rgb(s_rgb)
            s_block = ansi_block(s_rgb) if use_ansi else ""
            line += f"   {s_block} {s_hex} ({(s_frac_band or 0.0)*100:.1f}%)"

        print(line)

if __name__ == "__main__":
    main()
