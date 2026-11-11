# macOS distribution and native client contract

The CMake AppKit target creates `Battlefield 2.app`. It statically links the
portable C++/Rust setup libraries. No game data, installer EXE, patch payload,
key or profile belongs in its resources. `Resources/Setup` contains only the
path/size/hash manifest and licence notices.

Provide an existing universal native engine artifact directory using
`-DSETUP_CLIENT_ARTIFACT=/path/to/artifact`. Its contract is:

```text
artifact/
  bf2_client
  libdice_py.dylib
  libaidll.dylib
```

CMake's `bf2_setup_client` target copies **only these files** into `Contents/MacOS`,
alongside the assistant. Build the default target or `bf2_setup_client`; building
only `Battlefield2` does not run the import. This matches the client's existing
`@loader_path` rpath and `@rpath/libaidll.dylib` loading. Each artifact is checked
as executable/dylib Mach-O with both arm64 and x86_64, without symlinks, absolute
build dependencies or external rpaths. The source is never rewritten/re-signed.
Other files (including Info.plist) are not copied. Disabling the artifact option
and building removes the three earlier imported files from a reused build.
`-DSETUP_CLIENT_ALLOW_THIN=ON` (environment `SETUP_CLIENT_ALLOW_THIN=1` for the
script) is a developer override: it accepts an artifact built for the host
architecture only, prints a warning and checks dependencies/rpaths for that
architecture. It exists so the current x86_64-only `mac-metal` client can be
exercised on an Intel Mac before a universal engine build exists; `build-dmg.sh`
keeps rejecting non-universal bundles, so no such build becomes a disk image.
The DMG script additionally audits every Mach-O in the staged app. Runtime
libraries are supplied by the engine build; setup does not fetch them. Synthetic
fixtures test the import contract, without executing any client code. No actual
mac-metal artifact was located or bundled in that round.

Play launches only `Contents/MacOS/bf2_client`, with the verified generation as
its working directory, and `+modPath mods/bf2` as two arguments. It passes
`OPENBF2_DATA_ROOT=<generation>` and
`OPENBF2_SUPPORT_ROOT=~/Library/Application Support/OpenBF2` in the environment.
These environment hints are informational; the checked client source does not
consume them. Existing `Platform/macOS/ClientPaths.cpp` resolves Registry.con and
`@HOME@` profiles from `HOME/Library/Application Support/OpenBF2`, matching normal
assistant storage. `--test-home` redirects both assistant support and child HOME
to a direct child of the configured scratch root. No key is passed on the command
line or in the environment. The assistant verifies before Play, locks
`Setup/Install.lock`, resolves the current receipt again, and refuses launch if
the generation changed since verification. It retains that lock until NSTask
exits, prevents repeated Play and normal assistant quit while the game runs,
and reports launch/nonzero-exit errors. No command shell is involved.

The existing client uses a separate `Client.lock` for single-instance behavior.
Direct client launches do not acquire Install.lock; force-killing the assistant
also releases its guard. For a complete integration, a small client-side hook
must acquire/retain `Setup/Install.lock` itself (with a handoff protocol replacing
the parent's exclusive guard), or accept an inherited guard descriptor. Do not
have both processes independently take the same exclusive lock. No OpenBF2
source was edited in this round. Generation cleanup is not implemented, so setup
retains prior generations; runtime writes to immutable installed content remain
an integration audit item. Do not launch Windows data-tree tools.

Key file syntax is preserved:

```text
HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\EA Games\Battlefield 2\ergc = <20-character key>
```

Keys must pass the independently reconstructed retail local checksum for their
product; see [provenance and limits](cdkey-check.md). No online authentication
or key generation is implemented. The file is
private (0600 from exclusive creation); unrelated registry entries and profiles
are preserved. Case-insensitive names, duplicate entries, CRLF and the `\@`
fallback follow the existing client's syntax. A named value, even empty, takes
precedence over the fallback. Saving replaces all base-game key variants with
one normalized entry under the setup lock. Startup
requires a verified generation and locally valid stored keys before showing Play.

## Packaging

`macos/build-dmg.sh APP OUTPUT.dmg` makes a staging copy beside OUTPUT,
audits universal binaries, signs nested dylibs/client before the app, verifies
strictly, adds an Applications shortcut and runs `hdiutil create -format UDZO`.
The staging directory is always cleaned. Add `--check-only` as the third argument
to audit/stage/sign/verify without hdiutil; no DMG is created in that mode.
Ad-hoc signing (`-`) is the default
placeholder. `SETUP_SIGN_IDENTITY` selects an existing identity but the current
script does not constitute a complete hardened-runtime release-signing policy.
It must never re-sign the user's drag-copied app during first-run setup.

Earlier managed-session attempt: architecture and ad-hoc signature
verification passed; hdiutil failed `create failed - Device not configured`.
No DMG was produced. The same command needs to run in a normal macOS graphical
session with disk-image creation available. A later round exercised --check-only
successfully on the updated assistant and synthetic three-file client bundle;
it did not retry hdiutil or GUI startup.

## Release steps (documented, not executed)

1. The standalone licence is MIT. Finalize bundle identifier,
   Developer ID identity, minimum OS and engine artifact provenance.
2. Build/audit all universal code. Test the assistant and native client on both
   Intel and Apple silicon, including key paste, mounted DVD and ISO selection,
   retry/cancellation, fresh profiles, data locking and launch errors.
3. Sign nested code with the chosen identity and reviewed entitlements; sign the
   outer app with hardened runtime and timestamp. Review the actual client's
   requirements; do not add permissive entitlements speculatively.
4. Verify with `codesign --verify --strict --verbose=2`. Create/sign the DMG.
5. Submit the release artifact with `xcrun notarytool submit ... --wait` using
   owner-managed credentials. Staple using `xcrun stapler staple`, validate the
   ticket and perform Gatekeeper/fresh drag-copy/offline launch tests.

After reviewed Developer ID signing (nested dylibs/client first, outer app with
`--options runtime --timestamp` and only required entitlements), these are the
command shapes. Replace the artifact/profile placeholders;
do not submit the local ad-hoc image. Credentials must already be provisioned:

```sh
xcrun notarytool submit /path/to/Developer-ID-signed.dmg \
  --keychain-profile OWNER_NOTARY_PROFILE --wait
xcrun stapler staple /path/to/Developer-ID-signed.dmg
xcrun stapler validate /path/to/Developer-ID-signed.dmg
spctl --assess --type open --context context:primary-signature --verbose=2 \
  /path/to/Developer-ID-signed.dmg
```

Stapling a DMG does not staple its nested app. If an offline drag-copied app
needs its own ticket, submit a signed-app ZIP first, staple the app, then build,
sign, submit and staple the final DMG. Review the actual client entitlements and
ticket flow in the release session; this script is an ad-hoc packaging helper.

No notarization command, upload, signing-account change or Gatekeeper bypass
was run. Earlier native assistant GUI startup in this managed tool environment aborted
inside macOS `_RegisterApplication`, before user UI code ran. Compilation and
universal linkage pass; interactive UI behavior still needs a normal desktop
session. The optional `--smoke-test` mode exists for that session and writes six
window snapshots under the configured scratch root without saving a key.
The desktop click-through, scratch Application Support installation/reference
verification, low-space GUI and DMG checks are kept in the maintainers' internal
checklist, outside this repository.
