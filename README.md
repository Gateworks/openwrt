# Gateworks OpenWrt #
This Gateworks OpenWrt BSP layer is based off of upstream [OpenWrt](https://openwrt.org/) with Gateworks specific patches applied on top. A Gateworks specific Makefile is included to help facilitate building.

## Version Information ##
Each branch will be based off of an upstream [OpenWrt](https://openwrt.org/) version of our choosing. The version will be included in the branch as _<date>\_<openwrt version>_.

Each release branch will be named _<Last 2 Char of Year>.<Release Month>_. A release will be based off of the current Gateworks OpenWrt branch.

### Getting Version ###
The below information will also appear in the OpenWrt banner when booted. This can be found from
within OpenWrt in `/etc/banner`.

+ To get the OpenWrt version number the current branch is based off of, execute the `./scripts/getver.sh` script.
+ To get the Gateworks version number, execute the `./gateworks/scripts/version_number.sh` script.

## Building ##
The normal steps to follow for building Gateworks Openwrt is as follows:

1. `make -C gateworks/ imx6`
2. `make -C gateworks/ images/ventana`

Any subsequent rebuilding can be accomplished via the following: `make -j4 V=s`

The above will build images for the imx6 platform using the `gateworks/configs/imx6/.config` config file. The second step will grab the most recent release bootloader for the Ventana family and create jtagable images. For instructions on jtagging a board, please visit [this page](http://trac.gateworks.com/wiki/jtag_instructions).

For more information on Gateworks Openwrt and how to build, please visit the [Gateworks OpenWrt Building](http://trac.gateworks.com/wiki/OpenWrt/building) page. Also, please see the below #Targets section for additional targets in the Gateworks Makefile.

### Targets ###
The current Gateworks Makefile supports building of the following targets:

+ `setup`: Updates and installs package feeds. Normally not required to be executed by hand
+ `imx6`: Build imx6 target using `gateworks/configs/imx6/.config` config file
+ `cns3xxx`: Build imx6 target using `gateworks/configs/cns3xxx/.config` config file
+ `images`: Build both Ventana and Laguna product family jtagable images
+ `images/ventana`: Build Ventana jtagable images
+ `images/laguna`: Build Laguna jtagable images
+ `dirclean`: Clean temporary Gateworks files
+ `bootloader/ventana`: Get the latest Ventana product family bootloader
+ `bootloader/laguna`: Get the latest Laguna product family bootloader

### Tips and Tricks ###
The below sections will attempt to help speed development.

#### Source Downloads ####
In order to avoid downloading the same source tarballs' over and over again, we decided to create a symbolic link to the OpenWrt `dl` directory to `/usr/bin/dl` if it exists. This directory, however, can be changed by passing in the `DL_DIR=<path to dl dir>` when building, e.g. `DL_DIR=~/dl make -C gateworks/ imx6`.

This way, if there are multiple clones of the Gateworks OpenWrt BSP, or a workplace NFS shared between multiple developers, a source only has to be downloaded once saving both initial build times and shared bandwidth.

#### Multi-Threaded Building ####
In order to utilize the CPU to the fullest when building OpenWrt, we have decided to default the number of threads to use when building to the number of cores present on the build machine. However, if you require only a single thread, or want to use more build threads, the `J_ARG` environmental variable can be passed in on the command line, e.g. `J_ARG=-j8 make -C gateworks/ imx6`. This example will build OpenWrt using 8 threads.

#### Additional Profiles ####
If you want to make use of the Gateworks Makefile, you can add custom `.config` configuration files and build using them.

For example, if you want an imx6 profile that supports some module, but don't want to dirty up the original Gateworks `.config` file, the following could be done:

1. `make menuconfig` - make your changes
2. `cp .config gateworks/configs/imx6/.config_nousb`

Now, building with this new profile could be accomplished via the following: `PROFILE=_nousb make -C gateworks/ imx6`.

#### Bumping Feeds ####
Gateworks pins the `feeds.conf.default` file in order to provide a very consistent build artifacts. However, if there is an upstream package change that you require, bumping a sha in the `feeds.conf.default` file would accomplish this. For example, the packages feed is pinned to git sha 52e2f0e8, as seen here: `src-git packages https://github.com/openwrt/packages.git^52e2f0e8`.

In order to manually change it, edit the `feeds.conf.default` file and change the git sha after the `^` at the end of the string, e.g. change `src-git packages https://github.com/openwrt/packages.git^52e2f0e8` to `src-git packages https://github.com/openwrt/packages.git^52585203`. Generally, we take the first 8 characters of the commit id.

Alternatively, we have a script in the `gateworks/scripts/` directory that will automatically bump and pin all `git` feeds. See below for example runs.

+ In order to pin to the latest 'master' branch of each git feed, execute as follows:
 `./gateworks/scripts/pin-feeds feeds.conf.default master`

+ In order to let the script guess the branch to pin to, execute as follows:
 `./gateworks/scripts/pin-feeds feeds.conf.default`

After you have made your change, either rerun `make -C <target>`, or manually execute the update procedure via `make package/symlinks`. This will pull down the new package feeds and install them to the proper location.
