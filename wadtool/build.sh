#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
echo "Script dir is: $SCRIPT_DIR"
if ! [ -f "$SCRIPT_DIR/wadtool" ]; then
  echo "Compiling wadtool"
  gcc -fno-common -Wno-unused-result -O3 -c "$SCRIPT_DIR/decodes.c" -o "$SCRIPT_DIR/decodes.o"
  gcc -fno-common -Wno-unused-result -O3 -c "$SCRIPT_DIR/encode.c" -o "$SCRIPT_DIR/encode.o"
  gcc -fno-common -Wno-unused-result -O3 -c "$SCRIPT_DIR/imgproc.c" -o "$SCRIPT_DIR/imgproc.o"
  gcc -fno-common -Wno-unused-result -O3 -c "$SCRIPT_DIR/wadtool.c" -o "$SCRIPT_DIR/wadtool.o"
  gcc -fno-common -Wno-unused-result -O3 -c "$SCRIPT_DIR/mapconv.c" -o "$SCRIPT_DIR/mapconv.o"
  gcc "$SCRIPT_DIR/decodes.o" "$SCRIPT_DIR/encode.o" "$SCRIPT_DIR/imgproc.o" "$SCRIPT_DIR/mapconv.o" "$SCRIPT_DIR/wadtool.o" -o "$SCRIPT_DIR/wadtool"
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/bump.wad" ]; then
  echo "ERROR: Bumpmap WAD file is missing!"
  echo "Check out selfboot/bump.wad from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/wepn_decs.raw" ]; then
  echo "ERROR: Weapon decorations file is missing!"
  echo "Check out selfboot/tex/wepn_decs.raw from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/bfgg_nrm.cmp" ]; then
  echo "ERROR: BFG normal maps are missing!"
  echo "Check out selfboot/tex/bfgg_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/chgg_nrm.cmp" ]; then
  echo "ERROR: Chaingun normal maps are missing!"
  echo "Check out selfboot/tex/chgg_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/lasr_nrm.cmp" ]; then
  echo "ERROR: Unmaker normal maps are missing!"
  echo "Check out selfboot/tex/lasr_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/pisg_nrm.cmp" ]; then
  echo "ERROR: Pistol normal maps are missing!"
  echo "Check out selfboot/tex/pisg_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/plas_nrm.cmp" ]; then
  echo "ERROR: Plasma Rifle normal maps are missing!"
  echo "Check out selfboot/tex/plas_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/pung_nrm.cmp" ]; then
  echo "ERROR: Fist normal maps are missing!"
  echo "Check out selfboot/tex/pung_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/rock_nrm.cmp" ]; then
  echo "ERROR: Rocket Launcher normal maps are missing!"
  echo "Check out selfboot/tex/rock_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/sawg_nrm.cmp" ]; then
  echo "ERROR: Chainsaw normal maps are missing!"
  echo "Check out selfboot/tex/chgg_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/sht1_nrm.cmp" ]; then
  echo "ERROR: Shotgun normal maps are missing!"
  echo "Check out selfboot/tex/sht1_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi

if ! [ -f "$SCRIPT_DIR/../selfboot/tex/sht2_nrm.cmp" ]; then
  echo "ERROR: Super Shotgun  normal maps are missing!"
  echo "Check out selfboot/tex/sht2_nrm.cmp from github repo and run again."
  echo "Exiting."
  exit 255
fi


if [ -f "$SCRIPT_DIR/../selfboot/maps/map01.wad" ]; then
  if [ -f "$SCRIPT_DIR/../selfboot/alt.wad" ]; then
    if [ -f "$SCRIPT_DIR/../selfboot/pow2.wad" ]; then
      if [ -f "$SCRIPT_DIR/../selfboot/tex/non_enemy.tex" ]; then
        echo "Game data files have already been generated; exiting."
        exit 0
      fi
    fi
  fi
fi

echo "Running wadtool"
if [ -f "$SCRIPT_DIR/doom64.wad" ]; then
	echo "retail + nightdive"
	time "$SCRIPT_DIR/wadtool" "$SCRIPT_DIR/doom64.z64" "$SCRIPT_DIR/../selfboot" "$SCRIPT_DIR/doom64.wad"
	rm -f "$SCRIPT_DIR/../map34_nd.wad"
	rm -f "$SCRIPT_DIR/../map35_nd.wad"
	rm -f "$SCRIPT_DIR/../map36_nd.wad"
	rm -f "$SCRIPT_DIR/../map37_nd.wad"
	rm -f "$SCRIPT_DIR/../map38_nd.wad"
	rm -f "$SCRIPT_DIR/../map39_nd.wad"
	rm -f "$SCRIPT_DIR/../map40_nd.wad"
	rm -f "$SCRIPT_DIR/map34_nd.wad"
	rm -f "$SCRIPT_DIR/map35_nd.wad"
	rm -f "$SCRIPT_DIR/map36_nd.wad"
	rm -f "$SCRIPT_DIR/map37_nd.wad"
	rm -f "$SCRIPT_DIR/map38_nd.wad"
	rm -f "$SCRIPT_DIR/map39_nd.wad"
	rm -f "$SCRIPT_DIR/map40_nd.wad"
else
	echo "retail only"
	time "$SCRIPT_DIR/wadtool" "$SCRIPT_DIR/doom64.z64" "$SCRIPT_DIR/../selfboot"
fi

echo "Generated data files in specified selfboot directory: $SCRIPT_DIR/../selfboot"
echo "Done."
