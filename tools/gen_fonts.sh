#!/usr/bin/env bash
# Regenerates src/ui/fonts/*.h from the Inter typeface (SIL OFL 1.1).
#   pip install pillow fonttools && tools/gen_fonts.sh
set -euo pipefail
cd "$(dirname "$0")/.."

INTER_VERSION=4.1
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
curl -sSL -o "$WORK/inter.zip" "https://github.com/rsms/inter/releases/download/v${INTER_VERSION}/Inter-${INTER_VERSION}.zip"
unzip -q "$WORK/inter.zip" -d "$WORK/inter"
TTF="$WORK/inter/extras/ttf"
OUT=src/ui/fonts
mkdir -p "$OUT"
cp "$WORK/inter/LICENSE.txt" "$OUT/LICENSE-Inter.txt"

gen() { python3 tools/make_vlw.py "$TTF/$1" "$2" "$3" "$OUT/$4" "${@:5}"; }

gen Inter-Medium.ttf          12 kFontSmall     small.h
gen Inter-Regular.ttf         14 kFontBody      body.h
gen Inter-SemiBold.ttf        15 kFontBodyBold  body_bold.h
gen Inter-Bold.ttf            19 kFontTitle     title.h
gen InterDisplay-SemiBold.ttf 28 kFontLarge     large.h
gen InterDisplay-Light.ttf    80 kFontClock     clock.h --chars digits
gen InterDisplay-Light.ttf    50 kFontTimer     timer.h --chars digits
