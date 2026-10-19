#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
rm -f "$SCRIPT_DIR/decodes.o"
rm -f "$SCRIPT_DIR/encode.o"
rm -f "$SCRIPT_DIR/imgproc.o"
rm -f "$SCRIPT_DIR/wadtool.o"
rm -f "$SCRIPT_DIR/mapconv.o"
rm -f "$SCRIPT_DIR/wadtool"
# sorry Windows users, I made your life harder by overlooking this
rm -f "$SCRIPT_DIR/wadtool.exe"
rm -f "$SCRIPT_DIR/../selfboot/alt.wad"
rm -f "$SCRIPT_DIR/../selfboot/pow2.wad"
rm -f "$SCRIPT_DIR/../selfboot/tex/non_enemy.tex"
for i in {1..40}
do
  value=$(printf "%02d" $i)
  rm -f "$SCRIPT_DIR/../selfboot/maps/map$value.wad"
done
