# Doom 64 for Dreamcast (updated 2026/05/21) #

You don't have to know how to build a toolchain, or compile anymore.

You can upload a ROM and download a CDI.

Find more details here: https://colab.research.google.com/drive/1qkODLG1009ZPBHfMD7wF1qmcHxxrzZwt

Now for business as usual.

Do not open an issue for emulator-related problems. I close them without reading.

Important note: Doom 64 works and is stable when built against KOS v2.2.1. Also KOS master (v2.3.0) is known to work, with the caveat that it might break at any time.

**Please pay close attention to the README as significant new features have been added and the build instructions have changed.**


#### FEATURES ####

- Enemy, projectile and sector movement interpolation. Everything moves nicely and smoothly when the frame rate is above 30 FPS.


- CUSTOMIZABLE CONTROLLER MAPPINGS. See build instructions for details on how this works.


- UNCAPPED FRAME RATE, variable with correct physics. 60 FPS in the majority of the game with lights and normal mapping.


- 8bpp world textures. Increased performance when filtering is disabled. Reduces VRAM pressure / frequency of texture flushing (reduced frequency of graphical glitches in bonus levels).


- VMU SAVING IS SUPPORTED. **5 free blocks are required.**

Note: There must be a controller in the first controller port, and the VMU must be inserted into the first slot of the first controller.

Both settings and game passwords can be saved. Setting changes are saved automatically when you exit any settings related menu. If you notice a short pause when moving between menus, the settings file is being updated.

Game progress saving happens in the intermission screen between levels. Hit a button when the password is shown and you will be prompted to save. Loading game progress can be done at any time from the 'Password' menu. I have only been able to test this with OEM Sega VMU. It is known that Performance 4x Memory Pack does NOT work.

I have also implemented the original Controller Pak Management menus. Be careful, you can nuke your other saved games if you select the wrong one. Press Y+A together to select a file for deletion.


- Rumble/Vibration/Purupuru Pack is also now supported. Go to `Options`, `Movement`, and select `Rumble: StrikerDC`. Only the built-in feedback of RetroFighters StrikerDC wireless controller is supported currently. You may experience crashes or lack of feedback from other devices.


- Keyboard and mouse are also supported. I don't have much to comment on that, I got this from a pull request.


- Custom Knee Deep In The Dead content with Dreamcast-exclusive enhancements. Maps by z0k (with mods by Mittens). Music by Andrew Hulshult. 

Commercial redistribution of this additional content is not allowed. It is against the terms of use.

But because some of you are literal children and need it spelled out explicitly, any redistribution of this project with game data is copyright infringement and prohibited by law.


- The entire world and the all of the player's weapons have real-time normal mapping (DOT3 bump mapping, with tangent-space light direction vectors and three-dimensional normal texture maps, *that* normal mapping) that interacts with my custom dynamic lighting system (up to 16 simultaneous point sources, geometry tessellation for improved quality) (bUt WaIt, DoT3 BuMp MaPpInG oN tHe DrEaMcAsT iS iMpoSsIbLe?!?! because...)

![fragment shaders](https://github.com/jnmartin84/doom64-dc/blob/main/images/fragment.png?raw=true)


**VRAM captures, running on Dreamcast hardware**

![Lost Levels](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_6373.png?raw=true)

![Altar of Pain](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_24836.png?raw=true)

![Even Simpler](https://github.com/jnmartin84/doom64-dc/blob/main/images/doom64_18853.png?raw=true)

Lots of work went into this and I hope you all enjoy it.

You *will* have to do a tiny bit of actual work to get this going. If you don't have 30 to 45 minutes to spare, just go play on Steam and call it a day. The results are worth it though.


# build guide #

**Pre-requisites**

Whatever the directory you cloned this github repo to is named and wherever it is located, it will be referred to in this document as

`doom64-dc`

This guide will assume that you cloned it into your home directory. 

If you need to get to the top level of the repo, it will say

    cd ~/doom64-dc

The build is known to work on the following platforms as of the current commit:

    Debian (version?)
    Ubuntu 22.04
    Windows 11 - Cygwin 64-bit
    Windows (version?) - DreamSDK

It should work on most other Linux environments.

You will need a host/native GCC install and a full working Dreamcast/KallistiOS compiler toolchain install.

See [ https://dreamcast.wiki/Getting_Started_with_Dreamcast_development ] for instructions.

Doom 64 can be built using an unmodified copy of KOS cloned directly from the official repo. Use the `v2.2.1` tag version of KOS.

If you are going to file a Github issue, make sure you are using that version to test your problem.

Please follow the instructions for building KOS found in the wiki: [ https://dreamcast.wiki/Getting_Started_with_Dreamcast_development#Configuring_and_compiling_KOS_and_kos-ports ].

For best results, use the provided `environ.sh` to build KOS and Doom 64.

**How to generate Doom 64 disc image**

***N64 retail game support***

Somehow acquire a Doom 64 ROM in Z64 format and *name it all lowercase `doom64.z64` .*

Check that your Doom 64 ROM is a supported version (1.0, 1.1).

The below is the expected md5sum output for 1.0:

    md5sum doom64.z64
    b67748b64a2cc7efd2f3ad4504561e0e doom64.z64

The below is the expected md5sum output for 1.1:

    md5sum doom64.z64
    1b1378bb9ee819f740550f566745af73 doom64.z64

Now place a copy of `doom64.z64` in the `wadtool` directory.

***Nightdive Lost Levels support***

Buy the Nightdive Studios edition of Doom 64 (the 2020 PC release) from Steam or wherever.

Check that your Doom 64 IWAD is the correct one.

The below are two possible md5sum outputs that will lead to the correct N64 format maps being generated.

    md5sum DOOM64.WAD
    654c57d19f5c4a52cf8c63e34caa2fd2 DOOM64.WAD

or

    md5sum DOOM64.WAD
    0aaba212339c72250f8a53a0a2b6189e DOOM64.WAD

Now place a copy of the Doom 64 IWAD from the installation directory in the `wadtool` directory, *renamed to all lowercase `doom64.wad`.*

***Compiling Doom 64 for Dreamcast***

Go to the repo directory and compile it like any other KallistiOS project. Make sure you source your KOS environment first.

To build the source into an ELF file, run `make`.

    source /opt/toolchains/dc/kos/environ.sh
    cd ~/doom64-dc
    make clean
    make

As part of the build, `Make` will automatically build and run `wadtool`.

This should take a minute or less to run depending on your processor and disk speed.

The first terminal output you see should match the following except for the time values and a note about `retail` or `retail + nightdive` (the first time you run `make`):

    Script dir is: ~/doom64-dc/wadtool
    Compiling wadtool
    Running wadtool
    
    real   0m20.368s
    user   0m19.836s
    sys    0m0.233s
    Generated data files in specified selfboot directory: ~/doom64-dc/wadtool/../selfboot
    Done.

Subsequent runs will not rebuild `wadtool` but start at `Running wadtool` or skip that too if the generated files already exist in `selfboot`.

When it is complete, you will now have the following new files in the `~/doom64-dc/selfboot` directory:

    alt.wad
    pow2.wad
    tex/non_enemy.tex
    maps/map01.wad
    maps/...
    maps/map33.wad

If you provided a Nightdive Doom 64 IWAD you will also have the following additional files in `~/doom64-dc/selfboot`:

    maps/map34.wad
    maps/...
    maps/map40.wad

You now have all of the updated files required to run Doom 64 for Dreamcast in the places they need to be.

***Custom controller mappings***

You can now provide a customized controller mapping.

Before you make your disc image, create a file in `~/doom64-dc/selfboot` named `controls.ini`.

Note that Dreamcast START is still mapped to N64 START (pause, exit menus, etc).

The format is simple.

Each line consists of the game action, the number of Dreamcast buttons mapped to it
and the actual buttons to map to the action, if any. Values are separated by commas (,).
If you add any spaces, the mapping file will fail to parse. Sorry. It was this or nothing.

Valid game actions are one of: `RIGHT`, `LEFT`, `UP`, `DOWN`, `ATTACK`, `USE`, `AUTOMAP`, `SPEED`, `STRAFE`,
`STRAFELEFT`, `STRAFERIGHT`, `WEAPONBACKWARD`, `WEAPONFORWARD`.

Each action must start on its own line, and they must be present in the file in the order listed above.

The valid number of mapped buttons for each action is `0`, `1` or `2`.

If 0 buttons are mapped, the line ends at `0`.

If 1 button is mapped, a single button should be present, separated with a comma from the `1`.

If 2 buttons are mapped, two buttons should be present, separated with commas from the `2` and each other. Their ordering does not matter.

Valid Dreamcast buttons are one of: `DPAD_RIGHT`, `DPAD_LEFT`, `DPAD_UP`, `DPAD_DOWN`, `BUTTON_A`, `BUTTON_B`, `BUTTON_X`, `BUTTON_Y`, `TRIGGER_L`, `TRIGGER_R`.

If no `controls.ini` file is provided, the game defaults to the original hard-coded button mapping. Here is a working example of a `controls.ini` file for reference (it also happens to be the default control mapping):

    RIGHT,1,DPAD_RIGHT
    LEFT,1,DPAD_LEFT
    UP,1,DPAD_UP
    DOWN,1,DPAD_DOWN
    ATTACK,1,BUTTON_A
    USE,1,BUTTON_B
    AUTOMAP,2,BUTTON_X,BUTTON_Y
    SPEED,0
    STRAFE,0
    STRAFELEFT,1,TRIGGER_L
    STRAFERIGHT,1,TRIGGER_R
    WEAPONBACKWARD,1,BUTTON_X
    WEAPONFORWARD,1,BUTTON_Y

***Creating a disc image***

Once you have built (or downloaded) `doom64.elf` it is time to make a playable disc image from it.

If you have `mkdcdisc` installed and reachable from your `PATH`, you can use the `cdi` build target to create a self-booting CDI.

    cd ~/doom64-dc
    make cdi

This will produce an output file `doom64.cdi` in `~/doom64-dc`.

If you have `mkisofs` installed and reachable from your `PATH`, you can use the `dsiso` build target to create an ISO suitable for loading from SD card or hard drive with Dreamshell ISO loader.

    cd ~/doom64-dc
    make dsiso

This will produce an output file `doom64.iso` in `~/doom64-dc`.

**Alternative instructions for disc image generation on Windows**

If you are on Windows and unable to build `mkdcdisc`, there is another way to make a self-booting CDI.

Grab the latest patched `BootDreams` from: https://github.com/TItanGuy99/BootDreams/releases

Follow the previous instructions located earlier in this document to generate an ELF file.

Next, from a `Cygwin`/`Mingw`/`DreamSDK` terminal, go to the repo directory and generate `1ST_READ.BIN` from the ELF:

    cd ~/doom64-dc
    sh-elf-objcopy.exe -O binary build/doom64.elf build/doom64.bin
    /opt/toolchains/dc/kos/utils/scramble/scramble.exe build/doom64.bin selfboot/1ST_READ.BIN

Start `BootDreams` and make sure it is on the `DiscJuggler` setting.

Check `ISO settings` (under the `Extras` menu).

Make sure to select `Full Filenames`, `Joliet` and `Rock Ridge`.

 ![BootDreams settings](https://github.com/jnmartin84/doom64-dc/blob/main/images/bd_settings.png?raw=true)

Now return to the main `BootDreams` window.

Click the `Browse` button next to the `Selfboot folder` section.

Find your `~/doom64-dc/selfboot` folder in the Windows Folder selection dialog. Single-click on it to select/highlight it and click OK.

Change the `CD label` if you feel like it.

You can leave `Disc format` on the default setting.

Click the `Process` button.

Click `Yes` when prompted if you want to create a DiscJuggler image.

If you get Error dialog about `missing IP.BIN`, click `Yes` to create one.

When the file dialog pops up, pick a location for your CDI file, change the name if you'd like and click `Save`.

Once the `CDI4DC` window disappears and you get the `Created successfully` dialog, click `OK`.

You're ready to go and can burn the image to CD or process it further.

If you are trying to use any other tool or operating system to make an image, you are on your own.

Good luck. :-)

**Notes on GDEMU usage**

I do not own a GDEMU so I cannot vouch for the correctness of the following section.

I was informed that some extra configuration is required to run the CDI from GDEMU. I was told the following settings added to `GDEMU.INI`will get Doom 64 to run. (edited because I am getting conflicting information)

    image_tests = 0


# Acknowledgments

Immorpher [https://github.com/Immorpher] for introducing me to the entire Doom 64 RE scene, getting me hooked on hacking the N64 version. His role as Subject Matter Expert was invaluable, helping me see (and hear) all of the inaccuracies and bugs I missed along the way.

This port is heavily based off of his Merciless Edition and Doom 64 XE work [https://github.com/Immorpher/doom64xe]

which in turn would not be possible without the excellent work on reverse engineering Doom 64 by Erick194, found here [https://github.com/Erick194/DOOM64-RE]


Everyone on the KOS team and the Simulant Discord but especially

Falco Girgis [https://github.com/gyrovorbis] and Paul Cercueil [https://github.com/pcercuei] for their tireless work on fixing and extending the KOS PowerVR code and answering *NON-STOP* questions about "IS THIS SUPPOSED TO WORK AND WHAT STUPID DID I DO TO MAKE IT NOT WORK"

Luke Benstead [https://github.com/kazade] for the excellent glDC project, where I learned what near-z clipping is and how to implement it. Completely lost without that.

"Lobotomy" for creating some really nice normal maps and helping me learn how to do the rest

"erik5249" for developing a BC5 compression scheme for normal maps that helped me achieve my goal of keeping all (compressed) textures cached in RAM; also, providing a reference compressor and very performant decompressor

Andy Barajas [https://github.com/andressbarajas] for help debugging my filesystem use around my normal map implementation, and his libwav for streaming ADPCM music (bundled in the repo)

Ruslan Rostovtsev [https://github.com/DC-SWAT] for pointing me toward streaming ADPCM instead of using ADX for soundtrack and his excellent KOS PRs for fixing the streaming audio performance.

Piers Holdaway [https://github.com/Mittens0407] for doing performance modifications on the Knee Deep In The Dead/z0k content.

"StrikerTheHedgefox" [https://github.com/StrikerTheHedgefox] for starting with testing and moving on to many new additions to the project (motion interpolation, vmu stats). 

Donald Haase [https://github.com/QuzarDC] for helping get Doom 64 working on KOS `master` again.

Everyone who helped test builds along the way and offered endless moral support, thanks.

iD Software and Midway (🪦) for my favorite Doom game of all time.


## Since apparently this is very difficult to understand, I'll write it simply: Doom 64 is still for sale and is active IP. 

![Bethesda](https://github.com/jnmartin84/doom64-dc/blob/main/images/support.png?raw=true)

## ANY redistribution of CDI/ISO files with game data is software copyright infringement. It doesn't matter if it is free or for money. It is against the law. Asking me about this makes you look bad, makes the Dreamcast scene look bad and is not a good look for me either. Stop playing dumb and language lawyering over it. You aren't getting support so stop asking.
