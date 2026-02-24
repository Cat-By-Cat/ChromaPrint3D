#!/usr/bin/env python3
"""
Convert Lumina-style .npy LUT (and optional stacks) to ChromaPrint3D ColorDB JSON.

Standalone: only requires numpy. No dependency on Lumina-Layers.

Supported modes (auto-detected by entry count after reshape to Nx3):
  -   32 entries -> 2-color (2 channels, 5 layers, 2^5 recipes)
  - 1024 entries -> 4-color (4 channels, 5 layers, 4^5 recipes)
  - 1296 entries -> 6-color (6 channels, 5 layers, subset of 6^5)
  - 2738 entries -> 8-color (8 channels, 5 layers, curated selection)

Channel color names are inferred from "pure" recipes (c,c,c,c,c) which always
exist in every calibration board.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
# RGB -> Lab  (matches ChromaPrint3D core/include/chromaprint3d/color.h, D65)
# ---------------------------------------------------------------------------

def _srgb_to_linear(c: float) -> float:
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def _lab_f(t: float) -> float:
    delta = 6.0 / 29.0
    if t > delta ** 3:
        return t ** (1.0 / 3.0)
    return t / (3.0 * delta * delta) + 4.0 / 29.0


def rgb_to_lab(r: int, g: int, b: int) -> tuple[float, float, float]:
    """sRGB uint8 -> CIE L*a*b* (D65, same formula as ChromaPrint3D)."""
    lr = _srgb_to_linear(r / 255.0)
    lg = _srgb_to_linear(g / 255.0)
    lb = _srgb_to_linear(b / 255.0)
    x = 0.4124564 * lr + 0.3575761 * lg + 0.1804375 * lb
    y = 0.2126729 * lr + 0.7151522 * lg + 0.0721750 * lb
    z = 0.0193339 * lr + 0.1191920 * lg + 0.9503041 * lb
    Xn, Yn, Zn = 0.95047, 1.0, 1.08883
    fx, fy, fz = _lab_f(x / Xn), _lab_f(y / Yn), _lab_f(z / Zn)
    return (116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz))

# ---------------------------------------------------------------------------
# Auto-derive channel color name
# ---------------------------------------------------------------------------

_KNOWN_COLORS: dict[str, tuple[int, int, int]] = {
    "White":   (255, 255, 255),
    "Black":   (  0,   0,   0),
    "Cyan":    (  0, 134, 214),
    "Magenta": (236,   0, 140),
    "Red":     (193,  46,  31),
    "Yellow":  (244, 238,  42),
    "Blue":    ( 10,  41, 137),
    "Green":   (  0, 174,  66),
}

_KNOWN_LAB: dict[str, tuple[float, float, float]] | None = None


def _ensure_known_lab() -> dict[str, tuple[float, float, float]]:
    global _KNOWN_LAB
    if _KNOWN_LAB is None:
        _KNOWN_LAB = {name: rgb_to_lab(*rgb) for name, rgb in _KNOWN_COLORS.items()}
    return _KNOWN_LAB


def _lab_dist2(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def derive_color_name(r: int, g: int, b: int) -> str:
    """Map measured RGB to the nearest known filament color name in Lab space."""
    refs = _ensure_known_lab()
    lab = rgb_to_lab(r, g, b)
    best_name = "Unknown"
    best_d = math.inf
    for name, ref_lab in refs.items():
        d = _lab_dist2(lab, ref_lab)
        if d < best_d:
            best_d = d
            best_name = name
    return best_name


# ---------------------------------------------------------------------------
# Stack / recipe generation for 2- and 4-color (base^5 layout)
# ---------------------------------------------------------------------------

COLOR_LAYERS = 5


def _index_to_stack(index: int, base: int) -> tuple[int, ...]:
    """Convert flat LUT index to 5-layer stack (top-to-bottom digits of base-N number)."""
    digits = []
    t = index
    for _ in range(COLOR_LAYERS):
        digits.append(t % base)
        t //= base
    return tuple(digits[::-1])


def gen_stacks_base(base: int) -> list[tuple[int, ...]]:
    """All base^5 stacks in LUT index order."""
    return [_index_to_stack(i, base) for i in range(base ** COLOR_LAYERS)]


# ---------------------------------------------------------------------------
# Load external stacks file (6-color / 8-color)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 6-color: replicate Lumina's get_top_1296_colors() (deterministic)
#
# Hardcoded filament parameters from Lumina config.py SmartConfig.FILAMENTS.
# The algorithm: simulate all 6^5 color mixes, greedy-pick 1296 most diverse.
# ---------------------------------------------------------------------------

_6C_FILAMENTS = {
    0: {"rgb": (255, 255, 255), "td": 5.0},   # White
    1: {"rgb": (  0, 134, 214), "td": 3.5},   # Cyan
    2: {"rgb": (236,   0, 140), "td": 3.0},   # Magenta
    3: {"rgb": (  0, 174,  66), "td": 2.0},   # Green
    4: {"rgb": (244, 238,  42), "td": 6.0},   # Yellow
    5: {"rgb": (  0,   0,   0), "td": 0.6},   # Black
}
_6C_LAYER_HEIGHT = 0.08
_6C_TARGET = 1296
_6C_RGB_THRESHOLD = 8


def gen_stacks_6color() -> list[tuple[int, ...]]:
    """Reproduce Lumina's get_top_1296_colors(): greedy diverse selection from 6^5."""
    import itertools as _it

    backing = np.array([255, 255, 255], dtype=np.float64)
    alphas = {}
    for fid, props in _6C_FILAMENTS.items():
        bd = props["td"] / 10.0
        alphas[fid] = min(1.0, _6C_LAYER_HEIGHT / bd) if bd > 0 else 1.0

    candidates: list[dict] = []
    for stack in _it.product(range(6), repeat=COLOR_LAYERS):
        curr = backing.copy()
        for fid in stack:
            rgb = np.array(_6C_FILAMENTS[fid]["rgb"], dtype=np.float64)
            a = alphas[fid]
            curr = rgb * a + curr * (1.0 - a)
        candidates.append({"stack": stack, "rgb": curr.astype(np.uint8)})

    selected: list[dict] = []
    for c in range(6):
        pure = (c,) * COLOR_LAYERS
        for cand in candidates:
            if cand["stack"] == pure:
                selected.append(cand)
                break

    selected_stacks = {s["stack"] for s in selected}
    for cand in candidates:
        if len(selected) >= _6C_TARGET:
            break
        if cand["stack"] in selected_stacks:
            continue
        distinct = True
        cand_rgb = cand["rgb"].astype(np.int32)
        for s in selected:
            if np.linalg.norm(cand_rgb - s["rgb"].astype(np.int32)) < _6C_RGB_THRESHOLD:
                distinct = False
                break
        if distinct:
            selected.append(cand)
            selected_stacks.add(cand["stack"])

    if len(selected) < _6C_TARGET:
        for cand in candidates:
            if len(selected) >= _6C_TARGET:
                break
            if cand["stack"] in selected_stacks:
                continue
            selected.append(cand)
            selected_stacks.add(cand["stack"])

    return [s["stack"] for s in selected[:_6C_TARGET]]


def load_stacks_npy(path: str | Path) -> list[tuple[int, ...]]:
    """
    Load stacks from .npy. Accepts shapes (N,5), (N,) of 5-tuples.
    Returns list of N tuples, each length 5.
    """
    arr = np.load(path)
    if arr.ndim == 2 and arr.shape[1] == COLOR_LAYERS:
        return [tuple(int(x) for x in arr[i]) for i in range(arr.shape[0])]
    if arr.ndim == 1:
        out = []
        for i in range(arr.shape[0]):
            row = arr[i]
            if hasattr(row, '__len__'):
                out.append(tuple(int(x) for x in row[:COLOR_LAYERS]))
            else:
                raise ValueError(f"Stack element at index {i} is scalar, expected 5 values")
        return out
    raise ValueError(f"Unexpected stacks shape: {arr.shape}")


# ---------------------------------------------------------------------------
# Find pure-channel RGB from entries
# ---------------------------------------------------------------------------

def find_pure_rgbs(
    num_channels: int,
    stacks: list[tuple[int, ...]],
    measured: np.ndarray,
) -> list[np.ndarray]:
    """
    For each channel c in 0..num_channels-1, find the entry whose recipe is
    (c,c,c,c,c) and return its measured RGB.  Every calibration board must
    contain these pure recipes.
    """
    pure = [None] * num_channels
    for i, stack in enumerate(stacks):
        if i >= measured.shape[0]:
            break
        if len(set(stack)) == 1:
            c = stack[0]
            if 0 <= c < num_channels and pure[c] is None:
                pure[c] = measured[i].copy()
        if all(p is not None for p in pure):
            break

    for c in range(num_channels):
        if pure[c] is None:
            print(f"  Warning: pure recipe ({c},{c},{c},{c},{c}) not found; using first entry", file=sys.stderr)
            pure[c] = measured[0].copy()
    return pure  # type: ignore[return-value]


# ---------------------------------------------------------------------------
# Mode detection
# ---------------------------------------------------------------------------

_MODE_MAP = {32: 2, 1024: 4, 1296: 6, 2738: 8}


def detect_mode(total: int) -> int | None:
    return _MODE_MAP.get(total)


# ---------------------------------------------------------------------------
# Build ColorDB dict
# ---------------------------------------------------------------------------

def build_colordb(
    lut_path: str | Path,
    stacks_path: str | Path | None = None,
    name: str = "ColorDB from npy",
    material: str = "PLA Basic",
    line_width_mm: float = 0.42,
    layer_height_mm: float = 0.08,
    no_filter_blue: bool = False,
) -> dict:
    lut = np.load(lut_path)
    measured = lut.reshape(-1, 3).astype(np.uint8)
    total = measured.shape[0]
    num_channels = detect_mode(total)

    if num_channels is None:
        raise ValueError(
            f"Unsupported LUT size {total}. Expected one of: {sorted(_MODE_MAP.keys())}"
        )

    # -- Build stacks --------------------------------------------------------
    stacks: list[tuple[int, ...]]

    if num_channels <= 4:
        stacks = gen_stacks_base(num_channels)
    elif num_channels == 6:
        if stacks_path is not None:
            stacks = load_stacks_npy(stacks_path)
            print("  6-color: loaded stacks from file")
        else:
            print("  6-color: generating stacks (Lumina Smart 1296 algorithm)...")
            stacks = gen_stacks_6color()
    else:  # 8
        if stacks_path is None:
            raise ValueError("8-color LUT requires --stacks pointing to smart_8color_stacks.npy")
        stacks = load_stacks_npy(stacks_path)

    if len(stacks) < total:
        print(f"  Warning: stacks ({len(stacks)}) < LUT entries ({total}); truncating LUT", file=sys.stderr)
        total = len(stacks)
    elif len(stacks) > total:
        print(f"  Warning: stacks ({len(stacks)}) > LUT entries ({total}); truncating stacks", file=sys.stderr)
        stacks = stacks[:total]

    # -- Palette: derive from pure-channel RGB --------------------------------
    pure_rgbs = find_pure_rgbs(num_channels, stacks, measured)
    palette = []
    print("  Channel color inference:")
    for c in range(num_channels):
        rgb = pure_rgbs[c]
        color_name = derive_color_name(int(rgb[0]), int(rgb[1]), int(rgb[2]))
        palette.append({"color": color_name, "material": material})
        print(f"    ch{c}  RGB=({rgb[0]:>3},{rgb[1]:>3},{rgb[2]:>3})  ->  {color_name}")

    # -- 4-color blue-outlier filter (Lumina legacy) -------------------------
    skip_set: set[int] = set()
    if num_channels == 4 and not no_filter_blue:
        base_blue = np.array([30, 100, 200], dtype=np.float64)
        for i in range(min(len(stacks), total)):
            rgb = measured[i].astype(np.float64)
            if np.linalg.norm(rgb - base_blue) < 60 and 3 not in stacks[i]:
                skip_set.add(i)
        if skip_set:
            print(f"  4-color: filtered {len(skip_set)} blue outlier(s)")

    # -- Build entries -------------------------------------------------------
    entries = []
    for i in range(total):
        if i in skip_set:
            continue
        r, g, b = int(measured[i][0]), int(measured[i][1]), int(measured[i][2])
        lab = rgb_to_lab(r, g, b)
        entries.append({
            "lab": [round(lab[0], 4), round(lab[1], 4), round(lab[2], 4)],
            "recipe": list(stacks[i]),
        })

    return {
        "name": name,
        "max_color_layers": COLOR_LAYERS,
        "base_layers": 0,
        "base_channel_idx": 0,
        "layer_height_mm": layer_height_mm,
        "line_width_mm": line_width_mm,
        "layer_order": "Top2Bottom",
        "palette": palette,
        "entries": entries,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Convert Lumina-style .npy LUT to ChromaPrint3D ColorDB JSON.",
        epilog="Channel color names are auto-inferred from pure (c,c,c,c,c) recipes.",
    )
    ap.add_argument("lut", type=Path, help="Path to LUT .npy file")
    ap.add_argument("-o", "--output", type=Path, default=None, help="Output JSON path")
    ap.add_argument("--stacks", type=Path, default=None,
                    help="Path to stacks .npy (required for 8-color; optional override for 6-color)")
    ap.add_argument("--name", type=str, default="ColorDB from npy", help="Database name")
    ap.add_argument("--materials", type=str, default="PLA Basic",
                    help="Material name for all channels (default: 'PLA Basic')")
    ap.add_argument("--line-width-mm", type=float, default=0.42, metavar="W", help="Line width in mm")
    ap.add_argument("--layer-height-mm", type=float, default=0.08, metavar="H", help="Layer height in mm")
    ap.add_argument("--no-filter-blue", action="store_true",
                    help="Disable 4-color blue outlier filter (keep all 1024 entries)")
    args = ap.parse_args()

    if not args.lut.exists():
        print(f"Error: {args.lut} not found", file=sys.stderr)
        return 1

    out = args.output or args.lut.with_name(args.lut.stem + "_colordb.json")

    try:
        db = build_colordb(
            args.lut,
            stacks_path=args.stacks,
            name=args.name,
            material=args.materials,
            line_width_mm=args.line_width_mm,
            layer_height_mm=args.layer_height_mm,
            no_filter_blue=args.no_filter_blue,
        )
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    with open(out, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=4, ensure_ascii=False)

    n = len(db["entries"])
    print(f"\nWrote {n} entries -> {out}")
    print(f"  name            = {db['name']}")
    print(f"  layer_height_mm = {db['layer_height_mm']}")
    print(f"  line_width_mm   = {db['line_width_mm']}")
    print(f"  palette         = {[p['color'] for p in db['palette']]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
