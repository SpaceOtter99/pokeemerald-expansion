#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pokemon palette randomizer previewer for pokeemerald-expansion.

Features
- Browse a pokeemerald-expansion checkout and scan graphics/pokemon/**
- Searchable species dropdown + Shiny toggle
- Loads front sprite (PNG if present, else decodes 4bpp.lz) and palette (gbapal/.lz)
- Two groups (A/B) of 16 checkboxes mapped to palette indices (0..15)
- For each group, H / S / B "± range" spinboxes
- "Re-randomise" applies new random per-color deltas within those ranges
- Save recolored PNG

Dependencies: PyQt5, Pillow, numpy
    pip install PyQt5 pillow numpy
"""

import os
import sys
import glob
import random
import struct
import colorsys
from typing import List, Tuple, Optional

import numpy as np
from PIL import Image

from PyQt5 import QtCore, QtGui, QtWidgets

# ----------------------------
# Utils: GBA formats and tools
# ----------------------------

def gba_lz77_decompress(data: bytes) -> bytes:
    """
    Decompress GBA LZ77 (type 0x10) block.
    Ref: standard GBA LZ77 (0x10) format.
    """
    if not data or data[0] != 0x10:
        # Not LZ77 (just return as-is)
        return data
    # 3-byte little-endian size after 0x10
    size = data[1] | (data[2] << 8) | (data[3] << 16)
    out = bytearray()
    i = 4
    while len(out) < size and i < len(data):
        flags = data[i]
        i += 1
        for bit in range(8):
            if flags & (0x80 >> bit):
                if i + 1 >= len(data):
                    break
                b1 = data[i]
                b2 = data[i + 1]
                i += 2
                # length = top 4 bits of b1 + 3
                length = (b1 >> 4) + 3
                # disp = ((low 4 bits of b1) << 8) | b2 + 1
                disp = ((b1 & 0x0F) << 8) | b2
                disp += 1
                for _ in range(length):
                    out.append(out[-disp])
            else:
                if i >= len(data):
                    break
                out.append(data[i])
                i += 1
            if len(out) >= size:
                break
    return bytes(out[:size])

def read_palette_file(path: str):
    """
    Read a palette from one of:
      - *.gbapal (raw BGR555, 16 colors)
      - *.gbapal.lz (GBA LZ77; decompress then BGR555)
      - *.pal (JASC-PAL text format, 8-bit RGB triplets)

    Returns: list[16] of (R,G,B,A) tuples or None.
    """
    if not os.path.exists(path):
        return None

    lower = path.lower()
    if lower.endswith(".pal"):
        return read_jasc_pal(path)

    # Fallback: treat as gbapal (possibly LZ77-compressed)
    with open(path, "rb") as f:
        raw = f.read()
    if not raw:
        return None

    if raw[0] == 0x10:
        raw = gba_lz77_decompress(raw)

    if len(raw) < 32:
        return None

    colors = []
    for i in range(16):
        (val,) = struct.unpack_from("<H", raw, i * 2)
        # GBA BGR555: 0..4 red, 5..9 green, 10..14 blue
        r5 =  val        & 0x1F
        g5 = (val >> 5)  & 0x1F
        b5 = (val >> 10) & 0x1F
        r = int(round(r5 * 255 / 31))
        g = int(round(g5 * 255 / 31))
        b = int(round(b5 * 255 / 31))
        a = 0 if i == 0 else 255
        colors.append((r, g, b, a))
    return colors

def read_jasc_pal(path: str):
    """
    Read a JASC-PAL palette file.

    Expected format:
        JASC-PAL
        0100
        <count>
        R G B
        ...
    Returns a list of 16 RGBA tuples. If the file contains more than 16
    entries, the first 16 are used. If fewer, the last color is repeated.
    Index 0 is treated as transparent (alpha=0) to match the .gbapal convention.
    """
    if not os.path.exists(path):
        return None

    with open(path, "rt", encoding="utf-8", errors="replace") as f:
        lines = [ln.strip() for ln in f if ln.strip()]

    if not lines or lines[0] != "JASC-PAL":
        # not a JASC-PAL file
        return None

    # Accept 0100 or 0101; some tools write either
    if len(lines) < 3 or lines[1] not in ("0100", "0101"):
        return None

    try:
        count = int(lines[2])
    except ValueError:
        return None

    # Gather RGB triplets
    rgbs = []
    for ln in lines[3:3 + count]:
        parts = ln.split()
        if len(parts) < 3:
            continue
        try:
            r, g, b = (int(parts[0]), int(parts[1]), int(parts[2]))
        except ValueError:
            continue
        r = max(0, min(255, r))
        g = max(0, min(255, g))
        b = max(0, min(255, b))
        rgbs.append((r, g, b))

    if not rgbs:
        return None

    # Conform to 16 entries
    if len(rgbs) < 16:
        rgbs += [rgbs[-1]] * (16 - len(rgbs))
    elif len(rgbs) > 16:
        rgbs = rgbs[:16]

    # Apply GBA-like alpha convention (index 0 transparent)
    out = []
    for i, (r, g, b) in enumerate(rgbs):
        a = 0 if i == 0 else 255
        out.append((r, g, b, a))
    return out

def decode_4bpp_to_index_image(data: bytes, width=64, height=64) -> np.ndarray:
    """
    Decode a 4bpp tile stream into a 64x64 index image (tile order row-major).
    Each 8x8 tile is 32 bytes.
    """
    tiles_x = width // 8
    tiles_y = height // 8
    expected = tiles_x * tiles_y * 32
    if len(data) < expected:
        # If length is exactly 2048 for 64x64, good; otherwise pad
        data = data.ljust(expected, b'\x00')
    arr = np.zeros((height, width), dtype=np.uint8)
    off = 0
    for ty in range(tiles_y):
        for tx in range(tiles_x):
            # One tile = 8x8 pixels, 4bpp (2 px per byte)
            for row in range(8):
                for colpair in range(4):  # 8 px / 2 = 4 bytes
                    byte = data[off]
                    off += 1
                    lo = byte & 0x0F
                    hi = (byte >> 4) & 0x0F
                    x = tx * 8 + colpair * 2
                    y = ty * 8 + row
                    arr[y, x] = lo
                    arr[y, x + 1] = hi
    return arr

def rgba_palette_to_qpixmap(index_img, palette):
    """
    Map an index image (H x W, values 0..15) to RGBA and return QPixmap,
    without relying on PIL.ImageQt.
    """
    # palette -> (16,4) uint8
    pal = np.asarray(palette, dtype=np.uint8)

    # index_img (H,W) -> (H,W,4) RGBA
    rgba = pal[index_img]                     # shape (H,W,4), dtype=uint8
    rgba = np.ascontiguousarray(rgba)         # ensure contiguous for QImage

    h, w, _ = rgba.shape
    # Build QImage that views the numpy memory; bytesPerLine = w * 4
    qimg = QtGui.QImage(
        rgba.data, w, h, w * 4, QtGui.QImage.Format_RGBA8888
    ).copy()  # .copy() to detach from numpy buffer safely

    return QtGui.QPixmap.fromImage(qimg)

def pil_png_to_index_and_palette(img: Image.Image) -> Tuple[np.ndarray, List[Tuple[int,int,int,int]]]:
    """
    From a paletted PNG (mode 'P'), extract first 16 colors and index array.
    If not 'P', quantize to 16 colors.
    """
    if img.mode != 'P':
        img = img.convert('P', palette=Image.ADAPTIVE, colors=16)
    pal = img.getpalette()  # list of up to 256*3 ints
    colors = []
    for i in range(16):
        r = pal[i*3 + 0]
        g = pal[i*3 + 1]
        b = pal[i*3 + 2]
        a = 0 if i == 0 else 255
        colors.append((r,g,b,a))
    arr = np.array(img)
    # Clamp indices to 0..15
    arr = np.clip(arr, 0, 15).astype(np.uint8)
    return arr, colors

def jitter_color_with_deltas(color, dH_deg, dS_pct, dL_pct):
    r, g, b, a = color
    if a == 0:
        return color  # keep transparent

    # colorsys uses HLS (H in [0..1], L in [0..1], S in [0..1])
    import colorsys
    h, l, s = colorsys.rgb_to_hls(r/255.0, g/255.0, b/255.0)

    # Convert degrees/percent deltas to HLS deltas
    dh = (dH_deg / 360.0)
    ds = (dS_pct / 100.0)
    dl = (dL_pct / 100.0)

    h = (h + dh) % 1.0
    s = max(0.0, min(1.0, s + ds))
    l = max(0.0, min(1.0, l + dl))

    R, G, B = colorsys.hls_to_rgb(h, l, s)
    return (int(round(R*255)), int(round(G*255)), int(round(B*255)), a)

def apply_palette_randomized(base_palette, groupA, groupB,
                            ha, sa, ba,            # Group A ± ranges
                            hb, sb, bb,            # Group B ± ranges
                            decrease_only_A=False, # If True: S/V random ∈ [−2×range, 0]
                            decrease_only_B=False  # If True: S/V random ∈ [−2×range, 0]
                            ):
    """
    Sample one (ΔH,ΔS,ΔB) for A and one for B, then apply uniformly to all
    selected indices in those groups. If an index is in both, B wins.
    """
    def sample_triplet(h_range, s_range, b_range, dec_only):
        dh = random.uniform(-h_range, h_range)
        if dec_only:
            # S/V only decrease: random amount between 0 and −2×input
            ds = random.uniform(-2 * s_range, 0.0)
            db = random.uniform(-2 * b_range, 0.0)
        else:
            ds = random.uniform(-s_range, s_range)
            db = random.uniform(-b_range, b_range)
        return (dh, ds, db)

    # Sample one delta triplet per group
    dA = sample_triplet(ha, sa, ba, decrease_only_A)
    dB = sample_triplet(hb, sb, bb, decrease_only_B)

    newpal = list(base_palette)

    for idx in set(groupA):
        if 0 <= idx < 16:
            newpal[idx] = jitter_color_with_deltas(newpal[idx], *dA)

    # If overlap, let B overwrite A
    for idx in set(groupB):
        if 0 <= idx < 16:
            newpal[idx] = jitter_color_with_deltas(newpal[idx], *dB)

    return newpal


# -----------------------------------
# Repo scanning and sprite/palette IO
# -----------------------------------

class RepoIndex(QtCore.QObject):
    def __init__(self, root: str):
        super().__init__()
        self.root = root
        self.graphics = os.path.join(root, "graphics")
        self.pokemon_root = os.path.join(self.graphics, "pokemon")
        self.entries = []  # list of (label, species_path)
        self.rescan()

    def rescan(self):
        self.entries.clear()
        if not os.path.isdir(self.pokemon_root):
            return
        # include nested forms: graphics/pokemon/**/ where front/shiny palettes are present
        for path in glob.glob(os.path.join(self.pokemon_root, "**"), recursive=True):
            if not os.path.isdir(path):
                continue
            rel = os.path.relpath(path, self.pokemon_root).replace("\\", "/")
            if "/" in rel and rel.split("/")[0] == "egg":
                continue
            # check if this folder looks like a species folder
            has_any_sprite = any(os.path.exists(os.path.join(path, name)) for name in (
                "front.png", "anim_front.png", "front.4bpp.lz", "anim_front.4bpp.lz"))
            has_any_palette = any(os.path.exists(os.path.join(path, name)) for name in (
                "normal.gbapal", "normal.gbapal.lz", "shiny.gbapal", "shiny.gbapal.lz"))
            if has_any_sprite or has_any_palette:
                label = rel  # e.g., "deoxys/attack" or "bulbasaur"
                self.entries.append((label, path))
        self.entries.sort(key=lambda x: x[0].lower())

    def list_labels(self) -> List[str]:
        return [lbl for lbl,_ in self.entries]

    def path_for_label(self, label: str) -> Optional[str]:
        for lbl, p in self.entries:
            if lbl == label:
                return p
        return None

def load_palette_for_species(spec_dir: str, shiny: bool):
    """
    Try palettes in this order:
      shiny.gbapal(.lz) / normal.gbapal(.lz), then
      shiny.pal / normal.pal  (JASC-PAL)
    """
    names = []
    if shiny:
        names += ["shiny.gbapal", "shiny.gbapal.lz", "shiny.pal"]
    else:
        names += ["normal.gbapal", "normal.gbapal.lz", "normal.pal"]

    for name in names:
        p = os.path.join(spec_dir, name)
        pal = read_palette_file(p)
        if pal:
            return pal
    return None


def load_front_sprite(spec_dir: str, palette16: Optional[List[Tuple[int,int,int,int]]]) -> Tuple[np.ndarray, List[Tuple[int,int,int,int]]]:
    """
    Return (index_image, palette16). If PNG exists, derive from it (fallback to provided palette16 if PNG is RGB).
    Otherwise decode 4bpp.lz using palette16.
    """
    # Prefer PNGs
    png_path = None
    for name in ("front.png", "anim_front.png"):
        p = os.path.join(spec_dir, name)
        if os.path.exists(p):
            png_path = p
            break
    if png_path:
        img = Image.open(png_path)
        idx, pal = pil_png_to_index_and_palette(img)
        # If we already loaded a .gbapal, prefer that as the base palette (ensures normal vs shiny match)
        if palette16 is not None:
            pal = palette16
        return idx, pal

    # Else try 4bpp.lz
    lz_path = None
    for name in ("front.4bpp.lz", "anim_front.4bpp.lz"):
        p = os.path.join(spec_dir, name)
        if os.path.exists(p):
            lz_path = p
            break
    if lz_path:
        with open(lz_path, "rb") as f:
            raw = f.read()
        data = gba_lz77_decompress(raw) if raw and raw[0] == 0x10 else raw
        idx_img = decode_4bpp_to_index_image(data, 64, 64)
        # need a palette; if none provided, synthesize grayscale for preview
        if palette16 is None:
            palette16 = [(i*16, i*16, i*16, 255) if i else (0,0,0,0) for i in range(16)]
        return idx_img, palette16

    # Nothing found; return empty
    blank = np.zeros((64,64), dtype=np.uint8)
    if palette16 is None:
        palette16 = [(0,0,0,0)] + [(i*16, i*16, i*16, 255) for i in range(1,16)]
    return blank, palette16

# --------------------
# Qt UI implementation
# --------------------

def swatch_stylesheet(color: Tuple[int,int,int,int]) -> str:
    r,g,b,a = color
    if a == 0:
        # show checkerboard
        return ("QWidget {background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
                "stop:0 #c0c0c0, stop:0.5 #ffffff, stop:0.5 #c0c0c0, stop:1 #ffffff); "
                "border: 1px solid #888;}")
    return f"QWidget {{ background-color: rgba({r},{g},{b},{a}); border: 1px solid #888; }}"

class PaletteGroup(QtWidgets.QGroupBox):
    changed = QtCore.pyqtSignal()
    def __init__(self, title: str):
        super().__init__(title)
        self.checks: List[QtWidgets.QCheckBox] = []
        self.swatches: List[QtWidgets.QWidget] = []
        self.spin_h = QtWidgets.QDoubleSpinBox()
        self.spin_s = QtWidgets.QDoubleSpinBox()
        self.spin_b = QtWidgets.QDoubleSpinBox()
        self.chk_decrease_only = QtWidgets.QCheckBox("Decrease S/V only")
        self._build()

    def _build(self):
        layout = QtWidgets.QVBoxLayout(self)
        # controls row
        controls = QtWidgets.QHBoxLayout()
        for label, spin, maximum, suffix, step in (
            ("H ±", self.spin_h, 360.0, "°", 1.0),
            ("S ±", self.spin_s, 100.0, "%", 1.0),
            ("B ±", self.spin_b, 100.0, "%", 1.0),
        ):
            box = QtWidgets.QHBoxLayout()
            lab = QtWidgets.QLabel(label)
            spin.setRange(0.0, maximum)
            spin.setDecimals(1)
            spin.setSingleStep(step)
            spin.setValue(0.0)
            spin.valueChanged.connect(self.changed.emit)
            box.addWidget(lab)
            box.addWidget(spin)
            controls.addLayout(box)
        # Decrease-only toggle (affects S and V/B)
        self.chk_decrease_only.toggled.connect(self.changed.emit)
        controls.addWidget(self.chk_decrease_only)
        controls.addStretch(1)
        layout.addLayout(controls)

        # 16 checkboxes + swatches in 4x4 grid
        grid = QtWidgets.QGridLayout()
        for i in range(16):
            row, col = divmod(i, 4)
            hb = QtWidgets.QHBoxLayout()
            cb = QtWidgets.QCheckBox(f"{i:02d}")
            cb.toggled.connect(self.changed.emit)
            sw = QtWidgets.QWidget()
            sw.setFixedSize(22, 22)
            hb.addWidget(cb)
            hb.addWidget(sw)
            w = QtWidgets.QWidget()
            w.setLayout(hb)
            grid.addWidget(w, row, col)
            self.checks.append(cb)
            self.swatches.append(sw)
        layout.addLayout(grid)

    def selected_indices(self) -> List[int]:
        return [i for i, cb in enumerate(self.checks) if cb.isChecked()]

    def ranges(self) -> Tuple[float,float,float]:
        return (self.spin_h.value(), self.spin_s.value(), self.spin_b.value())

    def decrease_only(self) -> bool:
        """Whether S and V/B should only decrease (random in [−2×range, 0])."""
        return self.chk_decrease_only.isChecked()

    def set_swatches(self, palette16: List[Tuple[int,int,int,int]]):
        for i, sw in enumerate(self.swatches):
            sw.setStyleSheet(swatch_stylesheet(palette16[i]))

class ImagePane(QtWidgets.QWidget):
    def __init__(self, title: str):
        super().__init__()
        self.label_title = QtWidgets.QLabel(title)
        self.label_img = QtWidgets.QLabel()
        self.label_img.setAlignment(QtCore.Qt.AlignCenter)
        self.label_img.setMinimumSize(128, 128)
        lay = QtWidgets.QVBoxLayout(self)
        lay.addWidget(self.label_title)
        lay.addWidget(self.label_img)

    def set_pixmap(self, pm: QtGui.QPixmap):
        self.label_img.setPixmap(pm)

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("pokeemerald-expansion palette randomizer preview")
        self.resize(1100, 720)

        self.repo_idx: Optional[RepoIndex] = None
        self.base_palette: List[Tuple[int,int,int,int]] = [(0,0,0,0)]*16
        self.index_img: Optional[np.ndarray] = None

        # Top controls
        top = QtWidgets.QWidget()
        top_lay = QtWidgets.QHBoxLayout(top)

        self.path_edit = QtWidgets.QLineEdit()
        self.btn_browse = QtWidgets.QPushButton("Browse repo…")
        self.btn_browse.clicked.connect(self.on_browse)
        self.combo_species = QtWidgets.QComboBox()
        self.combo_species.setEditable(True)
        # Make it searchable with a completer
        self.combo_species.setInsertPolicy(QtWidgets.QComboBox.NoInsert)
        self.combo_species.lineEdit().textEdited.connect(self.on_species_filter)
        self.chk_shiny = QtWidgets.QCheckBox("Shiny")

        self.btn_reload = QtWidgets.QPushButton("Reload")
        self.btn_reload.clicked.connect(self.reload_repo)
        self.btn_save = QtWidgets.QPushButton("Save PNG…")
        self.btn_save.clicked.connect(self.on_save)

        for w in (QtWidgets.QLabel("Repo:"), self.path_edit, self.btn_browse,
                  QtWidgets.QLabel("Species:"), self.combo_species, self.chk_shiny,
                  self.btn_reload, self.btn_save):
            top_lay.addWidget(w)
        top_lay.addStretch(1)

        # Center: two previews
        center = QtWidgets.QWidget()
        center_lay = QtWidgets.QHBoxLayout(center)
        self.pane_orig = ImagePane("Original")
        self.pane_new  = ImagePane("Recolored")
        center_lay.addWidget(self.pane_orig, 1)
        center_lay.addWidget(self.pane_new, 1)

        # Bottom: group controls + buttons
        bottom = QtWidgets.QWidget()
        bottom_lay = QtWidgets.QHBoxLayout(bottom)
        self.grpA = PaletteGroup("Group A")
        self.grpB = PaletteGroup("Group B")
        self.grpA.changed.connect(self.update_preview)
        self.grpB.changed.connect(self.update_preview)

        controls_col = QtWidgets.QVBoxLayout()
        self.btn_random = QtWidgets.QPushButton("Re-randomise")
        self.btn_random.clicked.connect(self.update_preview)
        self.btn_reset = QtWidgets.QPushButton("Reset to base palette")
        self.btn_reset.clicked.connect(self.reset_palette)
        self.chk_auto = QtWidgets.QCheckBox("Auto-randomize")
        controls_col.addWidget(self.btn_random)
        controls_col.addWidget(self.btn_reset)
        controls_col.addWidget(self.chk_auto)
        controls_col.addStretch(1)

        self._auto_timer = QtCore.QTimer(self)
        self._auto_timer.setInterval(200)  # ms

        def _auto_tick():
            self.update_preview()

        self._auto_timer.timeout.connect(_auto_tick)
        self.chk_auto.toggled.connect(self.on_auto_toggle)

        bottom_lay.addWidget(self.grpA, 1)
        bottom_lay.addWidget(self.grpB, 1)
        bottom_lay.addLayout(controls_col, 0)

        # Layout main
        central = QtWidgets.QWidget()
        main_lay = QtWidgets.QVBoxLayout(central)
        main_lay.addWidget(top)
        main_lay.addWidget(center, 1)
        main_lay.addWidget(bottom)
        self.setCentralWidget(central)

        # Signals
        self.combo_species.currentTextChanged.connect(self.on_species_chosen)
        self.chk_shiny.toggled.connect(self.on_species_chosen)

        # Initialize
        self._species_model: List[str] = []
        self._species_filtered: List[str] = []

    # ------- Repo handling ----------
    def on_browse(self):
        path = QtWidgets.QFileDialog.getExistingDirectory(self, "Select repo root (contains 'graphics' folder)")
        if path:
            self.path_edit.setText(path)
            self.reload_repo()

    def reload_repo(self):
        root = self.path_edit.text().strip()
        if not root:
            QtWidgets.QMessageBox.warning(self, "Missing path", "Please select your pokeemerald-expansion root directory.")
            return
        self.repo_idx = RepoIndex(root)
        labels = self.repo_idx.list_labels()
        if not labels:
            QtWidgets.QMessageBox.warning(self, "Not found",
                                          "No species found under graphics/pokemon/**. Is this the correct repo?")
            return
        self._species_model = labels
        self._species_filtered = list(labels)
        self.combo_species.clear()
        self.combo_species.addItems(labels)
        # enable completer behavior
        completer = QtWidgets.QCompleter(labels, self.combo_species)
        completer.setCaseSensitivity(QtCore.Qt.CaseInsensitive)
        self.combo_species.setCompleter(completer)

    def on_species_filter(self, text: str):
        if not self._species_model:
            return
        self._species_filtered = [s for s in self._species_model if text.lower() in s.lower()]
        # Show filtered list in the dropdown popup without changing items list (keep simple)

    def on_species_chosen(self):
        if not self.repo_idx:
            return
        label = self.combo_species.currentText()
        spec_dir = self.repo_idx.path_for_label(label)
        if not spec_dir:
            return
        # Load palette
        pal = load_palette_for_species(spec_dir, shiny=self.chk_shiny.isChecked())
        # Load front sprite + palette (prefers PNG)
        idx_img, pal_from_sprite_or_given = load_front_sprite(spec_dir, pal)
        # If no gbapal available, use what the sprite gave us
        if pal is None:
            pal = pal_from_sprite_or_given

        self.base_palette = pal
        self.index_img = idx_img
        self.grpA.set_swatches(self.base_palette)
        self.grpB.set_swatches(self.base_palette)

        # Show original and recolored (initially same)
        pm = rgba_palette_to_qpixmap(self.index_img, self.base_palette)
        self.pane_orig.set_pixmap(pm)
        self.pane_new.set_pixmap(pm)

        self.update_preview()

    # ------- Preview / Save ----------
    def current_randomized_palette(self) -> List[Tuple[int,int,int,int]]:
        if self.base_palette is None:
            return [(0,0,0,0)]*16
        aidx = self.grpA.selected_indices()
        bidx = self.grpB.selected_indices()
        ha, sa, ba = self.grpA.ranges()
        hb, sb, bb = self.grpB.ranges()
        return apply_palette_randomized(
            self.base_palette,
            aidx, bidx,
            ha, sa, ba,
            hb, sb, bb,
            decrease_only_A=self.grpA.decrease_only(),
            decrease_only_B=self.grpB.decrease_only(),
        )

    def update_preview(self):
        if self.index_img is None:
            return
        newpal = self.current_randomized_palette()
        # update swatches for feedback
        self.grpA.set_swatches(newpal)
        self.grpB.set_swatches(newpal)
        pm = rgba_palette_to_qpixmap(self.index_img, newpal)
        self.pane_new.set_pixmap(pm)

    def reset_palette(self):
        if self.index_img is None:
            return
        self.grpA.set_swatches(self.base_palette)
        self.grpB.set_swatches(self.base_palette)
        pm = rgba_palette_to_qpixmap(self.index_img, self.base_palette)
        self.pane_new.set_pixmap(pm)

    def on_save(self):
        if self.index_img is None:
            return
        newpal = self.current_randomized_palette()
        h, w = self.index_img.shape
        rgba = np.array(newpal, dtype=np.uint8)[self.index_img]
        img = Image.fromarray(rgba, mode='RGBA')
        path, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save recolored PNG", "recolored.png", "PNG Images (*.png)")
        if path:
            img.save(path)

    def on_auto_toggle(self, checked: bool):
        if checked:
            # kick once immediately so user sees it start
            try:
                self.update_preview(randomize=True)
            except TypeError:
                self.update_preview()
            self._auto_timer.start()
        else:
            self._auto_timer.stop()

    def closeEvent(self, event):
        if hasattr(self, "_auto_timer") and self._auto_timer.isActive():
            self._auto_timer.stop()
        super().closeEvent(event)


# --------------------
# Entrypoint
# --------------------

def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()
