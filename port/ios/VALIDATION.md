# iOS validation

Updated: 2026-09-28. This runtime is based on upstream `16514a13`.
Build-specific observations are distinguished below. Automated build results
are visible in [GitHub Actions](https://github.com/NicholasDominici/halo-ce-ios/actions/workflows/ios.yml).

## Observed coverage

| Surface | Result |
| --- | --- |
| iPhone 17 Pro Max, A19 Pro | Player confirmed gameplay and audible sound on the earlier runtime; build 6 installed and displayed the profile menu at native 2868 × 1320 |
| Original NTSC-US Xbox maps, `01.10.12.2276` | Menu and first campaign map (`a10`) loaded; source maps unmodified |
| ARM64 iPhone simulator, iOS 27 | Menu, landscape layout, touch controls, icon, and removal of debug overlays checked |
| ARM64 iPad Pro 13-inch (M5) simulator, iOS 26 | Menu and `a10` opening scene rendered; software rendering slow |
| Existing saved data after an app update | Player profile and campaign save remained present |
| Physical iPad Pro 13-inch (M5), iPadOS 27 beta | Build 5 imported a real XISO and reached the opening campaign scene; build 6 displayed the menu at native 2752 × 2064 |
| Physical iPad orientation, build 6 | Player confirmed turning the iPad upright keeps the game horizontal with controls visible |

The final name/icon/overlay update was installed on the phone and visually
checked in the simulator. Physical gameplay and sound were confirmed on the
preceding runtime build. Build 6 menu rendering was checked on both physical
devices. A deployment target does not establish older-device coverage.

## Local toolchain

Apple Silicon Mac; Xcode 27 with iOS 27 SDK; Homebrew LLVM/LLD 23.1.2;
SDL 3.4.16; musl 1.2.5. iOS deployment target 16.0. PGO and LTO are disabled;
game assertions stay enabled. All guest imports resolved at startup.
The app executes signed native code without JIT or writable executable pages.

## Reproduce automated checks

```sh
brew install cmake ninja llvm lld sdl3 pkgconf
python3 tools/ios_test.py
python3 tools/ios_build.py --unsigned --ipa dist/Halo-CE-iOS-unsigned.ipa
python3 tools/ios_build.py --simulator
```

The regression suite passes locally and covers:

1. ILP32 layout, global and stack memory, indirect calls, atomics, and pointer
   zero-extension while running the translated guest from native signed pages.
2. Concurrent texture page tracking, fresh zeroed mappings, and preservation
   of a writable 4 KB tail beside read-only data on Apple's 16 KB pages.
3. 100 real SDL callbacks and 134,144 exact PCM samples, including reused guest
   buffers and growth beyond 64 KB. The original cross-thread SDL stream
   submission deadlocked this test; the iOS handoff submits on the callback thread.

4. Fifteen XISO importer tests under AddressSanitizer/UndefinedBehaviorSanitizer:
   exact byte copies, PAL/NTSC headers, whole-disc offsets, invalid/truncated
   images, missing maps, unsafe/duplicate names, directory cycles, mixed builds,
   cancellation/retry, existing destination preservation, and seeded corruption.

5. Twelve display-size cases under AddressSanitizer/UndefinedBehaviorSanitizer:
   native iPad/iPhone pixel dimensions, lower-resolution presets, original 4:3
   framing, non-integer aspect ratios, drawable/GPU limits, and invalid inputs.

## In-app XISO import (build 4)

The iPhone and iPad simulators imported a real NTSC-US XISO directly from the
app's Documents folder, then reached the Halo menu with active audio output.
All 24 extracted maps matched the original maps byte for byte (SHA-256), the save
sentinel remained intact, and temporary import directories were removed.
A local XCUITest also selected the real XISO through the native Files picker
and verified that game controls appeared after import. Another selected an
invalid image, checked the error, then selected the real image and reached the
game without restarting the app. A simulated interruption between backing up
and replacing maps recovered the originals on relaunch; valid maps skipped
extraction on subsequent launches. The iOS Xcode target explicitly enables ARC, and the importer rejects compilation
without it so asynchronous error strings remain valid.

On the physical M5 iPad, build 5 automatically imported a real NTSC-US XISO
copied into Documents and reached the opening campaign scene. The physical
Files-picker path and an iCloud-hosted image have not been exercised. Parser
cancellation is covered automatically; UI cancellation and cloud-provider
download behavior still need device testing.

## Native resolution and landscape (build 6, version 0.1.2)

The signed app was installed on the physical M5 iPad. Its console reported
`iOS render target: 2752x2064 (logical 640x480)` on the Apple M5 GPU, and a
2752 × 2064 device screenshot showed the complete landscape menu and controls.
The player confirmed that rotating the iPad upright keeps Halo horizontal with
all controls visible. SHA-256 hashes of all three persistent files under
`Documents/save/u` matched before and after the update. The original app bundle
identifier was retained, and previously imported maps remained available.

Build 6 was also installed and launched on the iPhone 17 Pro Max. The console
reported `iOS render target: 2868x1320 (logical 1042x480)`, and a device screenshot
showed the landscape profile menu and controls. All three persistent iPhone save
files matched their pre-update SHA-256 hashes. Native-resolution campaign
performance and long sessions have not been measured on either device.

An iPadOS 27 beta simulator (24A5355p) cold-started with a portrait canvas that
cropped landscape content despite reporting a landscape scene. This did not
occur in the physical iPad session above. Automated orientation screenshot
capture completed, but its images do not constitute a passing layout test.
Simulator portrait cold-start behavior remains a known issue.

CI runs these probes and compiles device and simulator apps without game data.
It does **not** play the game or validate a personal provisioning profile.
Its packaged IPA is unsigned and must be signed before installation.
The Android cleanup relocates the retained portable runtime, removes Android
services/build targets, and uses a bare-metal ELF assembler target before
embedding the compiled code into the iOS app.

## Still unverified

Extended physical iPad gameplay, older iPhones/iOS versions, a complete campaign,
physical controllers, physical-device network multiplayer, audio route changes/headphones,
and prolonged background/resume behavior. PAL maps are accepted by the cache
validator but have not been played on iOS. Bink intro videos are unsupported.

## Matchmaking demo, build 7

See [the demo validation](MATCHMAKING.md) for simulator multiplayer, bot
replication, Worker tests, device installation, and remaining live checks.
