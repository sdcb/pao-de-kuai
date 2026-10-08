"""Compare rendered UI screenshots against the release baseline.

The release criterion is "the five screenshots are visually identical to the baseline".  Eyeballing
is the final word, but a number makes regressions and near-misses visible, and this tool exists so
that judgement is repeatable instead of remembered.

Why it reports a noise floor: the scene under the screenshots is not deterministic (deal animation
timing, shuffle order) and the files are JPEG at quality 75, so the SAME binary rendered twice does
not produce identical bytes.  A baseline comparison is therefore only meaningful relative to that
floor -- `--floor` renders the current build twice and measures it, and the baseline numbers are
read against that.

Usage:
    python tools/compare_screenshots.py build-mingw-ucrt-x64 build-baseline
    python tools/compare_screenshots.py <dir-with-ui-*.jpg> build-baseline --floor
    python tools/compare_screenshots.py <a.jpg> <b.jpg>

Exit code is 0 when every pair is within tolerance, 1 otherwise, so it can gate a release check.
"""

import argparse
import os
import subprocess
import sys

from PIL import Image, ImageChops

# Above this mean absolute difference (0-255) a pair is not "visually identical" by any reading.
# The measured same-binary noise floor on this project is well under 1.0.
TOLERANCE_MAD = 2.0
# And above this fraction of clearly-different pixels, something moved on screen.
TOLERANCE_DIFF_FRACTION = 0.01
DIFF_PIXEL_THRESHOLD = 24

# Each entry is the screenshot name plus the scene_viewer arguments that produce it, mirroring
# the add_test lines in CMakeLists.txt.
IMAGES = [
    ("ui-start", ["--scene", "start"]),
    ("ui-settings", ["--scene", "settings"]),
    ("ui-game-deal", ["--scene", "game", "--mock", "deal"]),
    ("ui-game-play", ["--scene", "game", "--mock", "midgame"]),
    ("ui-result", ["--scene", "game", "--overlay", "result-win"]),
]


def resolve(directory, name):
    """The baseline files carry a `baseline-` prefix; the build output does not."""
    for candidate in (name + ".jpg", "baseline-" + name + ".jpg"):
        path = os.path.join(directory, candidate)
        if os.path.isfile(path):
            return path
    return None


def measure(a_path, b_path):
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    if a.size != b.size:
        return {"size_mismatch": "%sx%s vs %sx%s" % (a.size + b.size)}
    diff = ImageChops.difference(a, b)
    histogram = diff.convert("L").histogram()
    total = float(a.size[0] * a.size[1])
    # Mean absolute difference across the three channels, in 0-255 units.
    summed = 0
    for channel in range(3):
        channel_hist = diff.getchannel(channel).histogram()
        for value, count in enumerate(channel_hist):
            summed += value * count
    mad = summed / (total * 3.0)
    distinct = sum(count for value, count in enumerate(histogram) if value > DIFF_PIXEL_THRESHOLD)
    return {
        "mad": mad,
        "max": max(value for value, count in enumerate(histogram) if count),
        "fraction": distinct / total,
    }


def render_pair(build_dir, scene_args, out_prefix):
    """Renders the same view twice with the current build, to establish the noise floor."""
    viewer = os.path.join(build_dir, "scene_viewer.exe")
    if not os.path.isfile(viewer):
        return None
    paths = []
    for index in (1, 2):
        out = "%s-%d.jpg" % (out_prefix, index)
        subprocess.run([viewer] + scene_args + ["--screenshot", out], cwd=build_dir, check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        full = os.path.join(build_dir, out)
        if not os.path.isfile(full):
            return None
        paths.append(full)
    return paths


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("current", help="build directory (or a single jpg)")
    parser.add_argument("baseline", nargs="?", default="build-baseline",
                        help="baseline directory (or a single jpg)")
    parser.add_argument("--floor", action="store_true",
                        help="also render twice with the current build to report the noise floor")
    args = parser.parse_args()

    if os.path.isfile(args.current) and os.path.isfile(args.baseline):
        result = measure(args.current, args.baseline)
        print("%s vs %s: %s" % (args.current, args.baseline, result))
        return 0

    failures = 0
    print("%-16s %8s %6s %9s  %s" % ("image", "mean", "max", "moved", "verdict"))
    for name, _scene_args in IMAGES:
        current = resolve(args.current, name)
        baseline = resolve(args.baseline, name)
        if current is None or baseline is None:
            print("%-16s %8s %6s %9s  missing (%s / %s)" %
                  (name, "-", "-", "-", current is not None, baseline is not None))
            failures += 1
            continue
        result = measure(current, baseline)
        if "size_mismatch" in result:
            print("%-16s size mismatch: %s" % (name, result["size_mismatch"]))
            failures += 1
            continue
        ok = result["mad"] <= TOLERANCE_MAD and result["fraction"] <= TOLERANCE_DIFF_FRACTION
        print("%-16s %8.3f %6d %8.2f%%  %s" %
              (name, result["mad"], result["max"], result["fraction"] * 100.0,
               "OK" if ok else "DIFFERS"))
        if not ok:
            failures += 1

    if args.floor:
        # The right control is per image, not global: a static menu renders almost identically twice
        # while the settings modal (enter animation, blinking caret) and the deal animation do not.
        # Comparing a baseline difference against a floor measured from a DIFFERENT scene would
        # either hide a real regression or invent one.
        print("\n%-16s %9s %9s  %s" % ("image", "vs base", "floor", "verdict"))
        for name, scene_args in IMAGES:
            pair = render_pair(args.current, scene_args, "noise-" + name)
            if pair is None:
                print("%-16s %9s %9s  could not render" % (name, "-", "-"))
                continue
            floor = measure(pair[0], pair[1])
            for path in pair:
                os.remove(path)
            current = resolve(args.current, name)
            baseline = resolve(args.baseline, name)
            if current is None or baseline is None:
                continue
            versus = measure(current, baseline)
            # A regression is a difference clearly beyond what the same build produces by itself.
            ratio = versus["mad"] / floor["mad"] if floor["mad"] > 0.02 else float("inf")
            verdict = "same as its own floor" if ratio <= 2.0 else "BEYOND floor (x%.1f)" % ratio
            print("%-16s %9.3f %9.3f  %s" % (name, versus["mad"], floor["mad"], verdict))

    print("\n%s" % ("all five screenshots are within tolerance" if failures == 0
                    else "%d of %d screenshots differ" % (failures, len(IMAGES))))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
