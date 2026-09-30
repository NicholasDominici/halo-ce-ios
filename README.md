<img src="port/ios/Assets.xcassets/AppIcon.appiconset/AppIcon-180.png" width="96" alt="Halo: CE app icon">

# Halo: CE for iPhone and iPad

[![iOS build](https://github.com/NicholasDominici/halo-ce-ios/actions/workflows/ios.yml/badge.svg?branch=ios-port)](https://github.com/NicholasDominici/halo-ce-ios/actions/workflows/ios.yml)

Halo: Combat Evolved running natively on iPhone and iPad. It's a port of the
Halo decompilation projects (see [credits](#credits)), compiled for ARM64 with
OpenGL ES 3 graphics, SDL audio, and on-screen touch controls.

You'll need to provide your own Halo: CE XISO, which the app imports right on
your device.

I've been testing it on an iPhone 17 Pro Max and an iPad Pro 13-inch (M5).
The iPad imports a XISO on-device and runs at its native 2752 × 2064 resolution.
It stays horizontal when turned upright. It's still early, so if you hit a bug,
please [open an issue](#reporting-bugs).

![Halo's main menu with the on-screen controls](docs/ios/menu.png)

## What you need

- An iPhone or iPad on iOS 16 or newer
- Your own Halo: CE XISO, from the original Xbox release (NTSC-US or PAL)
- An Apple account to sign the app

The importer checks the map build number and accepts `01.10.12.2276` (NTSC-US)
and `01.01.14.2342` (PAL).

## Installing

1. Download `Halo-CE-iOS-unsigned.ipa` from
   [Releases](https://github.com/NicholasDominici/halo-ce-ios/releases). If you
   want the very latest build, every
   [Actions run](https://github.com/NicholasDominici/halo-ce-ios/actions/workflows/ios.yml)
   also uploads one as `halo-ce-ios-unsigned`.
2. Sign and install it with your Apple account. New to sideloading?
   [AltStore](https://faq.altstore.io/) and [Sideloadly](https://sideloadly.io/faq)
   both have guides. You can also build and sign it yourself with Xcode
   ([see below](#building-from-source)).
3. Open **Halo: CE**, tap **Choose Halo XISO**, and pick your disc image in Files.

The app checks the image, pulls the maps out of it (there's a progress bar), and
starts the game when it's done. After that, it opens straight into Halo.

![The XISO import screen](docs/ios/import.jpg)

You can also drop your `.iso` or `.xiso` into the Halo: CE folder yourself
(Files app > On My iPhone > Halo: CE, or your iPhone's Files tab in Finder) and
then open the app. It'll find the image and import it automatically. Once
that's done, you can delete the ISO from the folder to free up the space.

### Updating

Sign new versions with the same bundle ID and your saves carry over. They live
in the `save` folder inside Halo: CE's folder in Files, so back that up before
you delete the app.

## Controls

- **Left stick** to move, **right stick** to look
- **A** jump / select, **B** melee / back, **X** reload / use, **Y** switch weapons
- **Arrow buttons** for menus
- **FIRE**, **GRENADE**, **CROUCH**, **ZOOM**, **LIGHT** (flashlight),
  **SWAP G** (switch grenades), and **PAUSE** each get their own button

Tap **Hide controls** for a clean screen, and **Show controls** to bring them back.

## Matchmaking demo (development branch)

The `matchmaking-demo` branch adds a **Multiplayer** button. At Halo’s main menu,
choose **Practice with bots** to play eight-player Blood Gulch Slayer locally,
with seven host-controlled bots. They use Halo’s normal movement, weapons,
damage, respawn, and scoring. Their behavior is basic; they do not use vehicles
or plan routes around the whole map.

For real players, deploy [the matchmaking service](services/matchmaking/README.md)
and enter its HTTPS URL on each device, then choose **Find match**. Compatible
clients queue together for 20 seconds. The first device hosts; the host fills
remaining seats with bots based on players that actually joined Halo. Use
**Back to Halo** once the match starts, or **Cancel search / leave match** to
return to the menu before starting another search.

This is an early demo. There is no public default endpoint, ranked matching,
host migration, or relay for restrictive NAT. Clients need matching map data.
Matchmaking coordinates our native clients; it does not connect to the web port.
[Crossplay research](research/web-crossplay.md) records the public web transport
and the integration work needed with its author.

## Display settings

Halo runs at native display resolution by default and stays in landscape on
iPhone and iPad. You can lower the render resolution or use original 4:3
framing in `Documents/config.toml`; see [resolution settings](port/ios/README.md#resolution).
An in-app graphics menu and anti-aliasing are not implemented yet.

The [validation record](port/ios/VALIDATION.md) lists what has been tested and
known issues, including a cropped portrait canvas on an iPadOS 27 beta simulator
cold start.

## Building from source

You'll need an Apple Silicon Mac with Xcode and Python 3. I'm using Xcode 27 and
Homebrew's LLVM 23.1.2.

```sh
git clone https://github.com/NicholasDominici/halo-ce-ios.git
cd halo-ce-ios
brew install cmake ninja llvm lld sdl3 pkgconf

# Run the tests
python3 tools/ios_test.py

# Build and sign for your own device
# (add your Apple account in Xcode > Settings > Accounts first)
python3 tools/ios_build.py --team YOUR_TEAM_ID --bundle-id com.yourname.haloce

# Or build an unsigned IPA to sign later
python3 tools/ios_build.py --unsigned --ipa dist/Halo-CE-iOS-unsigned.ipa

# Or build for the simulator
python3 tools/ios_build.py --simulator
```

The build script downloads SDL, musl, and the Khronos GL headers by itself. The
[iOS guide](port/ios/README.md) goes deeper on installing from the command line,
troubleshooting, and the internals.

If you'd rather extract the maps on your Mac, there's a script for that too, and
the guide shows [how to copy them over](port/ios/README.md#optional-manual-extraction):

```sh
python3 tools/ios_extract_assets.py /path/to/Halo.iso --output assets
```

## How it works

Halo's code expects 32-bit pointers. The port compiles the game for ARM64 with
32-bit pointers, rewrites its memory accesses to land inside a 4 GB block of
memory, and runs it straight out of the signed app.

- `port/runtime` is the portable runtime
- `port/ios` is the iOS app: UIKit, touch controls, audio, the loader, and the
  XISO importer
- `port/linux` has the renderer and the Xbox compatibility layer, which come
  from the upstream Linux port

## Reporting bugs

[Open an issue](https://github.com/NicholasDominici/halo-ce-ios/issues) with
your device, iOS version, the app version, and what you were doing when it went
wrong. Logs help a ton: `ios-runtime.log` and `debug.txt` are in the Halo: CE
folder in Files. Give them a quick skim for anything personal before you post
them.

## Credits

This is built on a lot of other people's work:

- [punpckhdq/halo](https://github.com/punpckhdq/halo), the original Halo: CE
  decompilation
- [bnunu/halo-1](https://github.com/bnunu/halo-1), bnunu's fork of the
  decompilation
- [cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal),
  the native ports this one is based on, including the ARM64 runtime,
  renderer, and audio code

The XISO importer builds on [extract-xiso](https://github.com/XboxDev/extract-xiso).
This product includes software developed by in <in@fishtank.com>.

The code is CC0, same as upstream (see [LICENSE.md](LICENSE.md)). Third-party
notices are in [THIRD_PARTY.md](port/ios/THIRD_PARTY.md).

This is a fan project. Halo, Master Chief, and all of the game's art and assets
belong to Microsoft.
