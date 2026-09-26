# `dist-apple`

`mcpp.dist.apple` lays out the staged tree as a macOS or iOS application bundle, signs it, writes a disk image, and runs the bundle under `mcpp run --format app`.

## `dist-apple`

Module `mcpp.dist.apple`; engine floor: 2026.9.14.2 (0.10.0); 2026.9.11.2 (macOS) and 2026.9.12.3 (iOS) before it.

**Needs and behaviour.** the base macOS install (`ditto`, `codesign`, `hdiutil`), and `xim:macapp-run` for `mcpp run` on macOS, which this feature declares with `when = "run"`. macOS: `Contents/`-shaped, as always. iOS (`aarch64-ios-sim`, `aarch64-ios`): a flat bundle at the same call site -- no separate feature, no separate module -- with `MinimumOSVersion` from `mcpp::min_platform_version()` (#622 A11), `CFBundleSupportedPlatforms` read from `env == "sim"`, `UIDeviceFamily`, `LSRequiresIPhoneOS`, and a directory of flat PNGs listed under `CFBundleIcons` in place of macOS's single `.icns` file. Signing is skipped on the simulator row (`options::identity` is ignored, with a `mcpp::warning` naming why), and the device row signs only with an identity. The iOS row is measured end to end on `macos-15`: a real `mcpp build`, `mcpp pack --format app` and `mcpp run` against `aarch64-ios-sim`, through `xim:apple-simulator-tools`' `simctl-run`. **The macOS floor is one release higher than its siblings** and the reason is not this member: under 2026.9.11.1 `mcpp pack` staged before dispatching and let a staging failure fail the command, so on a Mach-O program -- which the built-in closure walk refuses, because it uses `LD_TRACE_LOADED_OBJECTS` and dyld answers that by running the program -- every dispatched format was unreachable, including one that reads no staged tree. 2026.9.11.2 makes staging a service to the provider. From 0.9.2 the staged tree's deployed files (`bin/<to>/...`, which the engine stages for a Mach-O program before the closure walk since the release for mcpp#630) land at the bundle's resource destination -- `Contents/Resources/<to>/...` on macOS, the bundle root on iOS -- and the launcher alone goes to the executable directory, so `CFBundleExecutable` names a file that is where it says. The iOS fixture declares `llvm.libcxx` and `llvm.compiler-rt-builtins` under `cfg(os = "ios")`, which is what an application that imports `std` on those rows declares. From 0.10.0, with mcpp 2026.9.14.2: the dylibs the engine stages beside a Mach-O program, which the stage manifest's `needs` lines name, go to `Contents/Frameworks/` (`Frameworks/` on iOS) and not to the resources; the program is linked with the rpath that finds them there (`@executable_path/../Frameworks`, `@executable_path/Frameworks` on iOS) through `mcpp::link_flag`, so no file is edited after the link; a macOS bundle without `options::identity` is signed ad hoc, frameworks first and the bundle second, which `codesign --verify --deep --strict` requires of a bundle that carries a framework; an incomplete closure is a `mcpp::warning` naming the unresolved libraries; every refusal is a `mcpp::warning` as well, because the engine discards a build program's output when it exits 0. On macOS the member supplies the runner named `app` (`macapp-run`), so `mcpp run --format app` runs the bundle's executable in the foreground and returns its status with no runner in the manifest; a manifest runner of that name wins. `--format dmg` stages the bundle beside an `Applications` link and writes a UDZO image with `hdiutil create` (`options::volume_name`, `options::dmg`); it is refused on iOS. An engine below 2026.9.14.2 stages no `needs` lines, so the bundle carries no framework, anchors the rpath to the package directory, and hands the bundle directory to the kernel under `mcpp run --format app` unless `--runner app` is typed. CI measures the bundle on `macos-15`: the load command, the signature, the program with and without its framework (exit 7, then "Library not loaded"), `mcpp run --format app` with and without `--runner app`, and `hdiutil verify` and an attached image. From 0.11.0: a project's own Info.plist entries (`options::info_plist`), an iOS device bundle's provisioning profile (`options::provisioning_profile`), and `devicectl-run` (`xim:apple-device-tools`) as the device row's runner named `app` -- see [`dist-apple`: a project's Info.plist, and an iOS device](#dist-apple-a-projects-infoplist-and-an-ios-device). From 0.12.0 `options::omit_keys` leaves out a key the member only defaults -- see [`dist-apple`: a project's Info.plist, and an iOS device](#dist-apple-a-projects-infoplist-and-an-ios-device). From 0.12.0 a package in the resolved graph contributes Info.plist entries through `[package.metadata.dist-apple]`, applied before the application's own

## `dist-apple`: a project's Info.plist, and an iOS device

| option | what it does |
|---|---|
| `info_plist` | A plist whose top-level `<dict>` entries join the bundle's Info.plist: usage descriptions, URL types, background modes. A key the member derives is refused by name: `CFBundleExecutable`, `CFBundleIdentifier`, `CFBundleName`, `CFBundleShortVersionString`, `CFBundleVersion`, `CFBundlePackageType`, `LSMinimumSystemVersion`, `MinimumOSVersion`, `CFBundleSupportedPlatforms`, `CFBundleIconFile`, `CFBundleIcons`. A key it only defaults takes the project's value: `UIDeviceFamily`, `LSRequiresIPhoneOS`, `NSHighResolutionCapable`. Without `info_plist` the plist is byte-identical to 0.10.1's |
| `omit_keys` | From 0.12.0. Defaulted keys to leave out of the Info.plist, for a project whose other build states neither: `UIDeviceFamily`, `LSRequiresIPhoneOS`, `NSHighResolutionCapable`. A property list has no null, so `info_plist` can replace a default's value but cannot remove its key. A key the member derives, a key it never writes, a key named twice, and a key `info_plist` also sets are each refused by name. A key that does not apply to the row (`UIDeviceFamily` on macOS) changes nothing |
| `graph_info_plist` | From 0.12.0, `true` by default: the Info.plist entries the resolved graph's packages state (see below). `false` reads none |
| `provisioning_profile` | The device row (`aarch64-ios`) only, and only with `identity`. The profile is embedded as `embedded.mobileprovision`. Its `Entitlements` dictionary signs the bundle unless `entitlements` names a file, with a wildcard `application-identifier` stated as the bundle's own. That identifier must cover the bundle identifier: exactly, `<team>.*`, or `<team>.<prefix>.*`. The plist is read from the profile's bytes, so a plan made on any host checks it; verifying the profile's CMS signature is left to `codesign` and the device |

A library states its Info.plist entries in its own manifest (0.12.0, mcpp
2026.9.16.1), as a plist of the shape `info_plist` takes, its path relative to
the library's directory:

```toml
# the library's mcpp.toml
[package.metadata.dist-apple]
info_plist = "apple/Info-fragment.plist"
```

The entries of every package in the resolved graph other than the application
are applied in the graph's order, dependencies first, so a package overrides the
packages it depends on; the application's own `info_plist` is applied last and
wins every key, and a key the application names in `omit_keys` is left out
whoever contributes it. A key the member derives is refused in a contribution as
in `info_plist`, naming the package. Under an older engine there is no graph, and
nothing is contributed.

On the device row the member supplies the runner named `app`: `devicectl-run`,
from `xim:apple-device-tools`, which the feature declares with `when = "run"` on
that row alone. `mcpp run --target aarch64-ios --format app` installs the signed
bundle on a connected device with `xcrun devicectl device install app`, then
launches it with the console attached. `DEVICECTL_RUN_DEVICE` names a device;
unset, the first connected iOS device is used. The simulator row keeps
`simctl-run`.

**Not yet measured:** no runner this repository uses has a device, and the
device bundle, its signature and whether `devicectl` returns the program's exit
status have not been measured on one; `devicectl-run` prints that when it runs.
CI measures the rest:

- On Linux, the plan: the entries, a refused key, an omitted default on iOS
  and on macOS and the three refusals of `omit_keys`, the graph's entries in
  the graph's order with the application's winning and `graph_info_plist =
  false`, a contributed derived key refused naming its package, an unreadable
  graph document refused, the embedded profile, the
  entitlements, a wildcard profile, and the refusals of a profile for another
  identifier, on the simulator row, or without an identity.
- On `macos-15`: the entries in a real macOS bundle that `plutil` accepts and
  that launches, and in a simulator bundle that the simulator runs.
