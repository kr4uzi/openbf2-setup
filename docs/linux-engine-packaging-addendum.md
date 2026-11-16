# Linux engine packaging integration

OpenBF2 builds the native client; this repository embeds it in the installer
and launcher. Follow the same two-repository assembly model as macOS.
The authoritative artifact schema and commands are in
[Linux distribution](linux-distribution.md#release-inputs-and-baseline).

## Responsibilities

The engine build supplies the client artifact and provisions the matching SDK
and runtime artifact. This repository owns the frontend, client embedding,
artifact validation and final application packaging. Workflow integration and
release uploads belong in the engine repository.

The existing Linux dedicated-server archive must remain available. Add a
distinctly named client application asset and checksum without replacing the
server asset.

## SDK and artifacts

Target generic x86-64 and glibc 2.35. Confirm the proposed SDK pins in
`linux/toolchain.json` before provisioning. Build the engine and bundled
dependencies against that baseline; a newer compiler alone does not lower the
libc requirements of prebuilt libraries.

Both artifact metadata records must carry the same immutable SDK image identity
(`registry/image@sha256:...`) and SDK toolchain object. Rust and Boost fields
describe setup prerequisites, not additional engine source dependencies.
Record source revisions, dependency versions, source hashes, licenses and
complete file inventories. SDK provisioning is not implemented in this repository.

The engine artifact contains `bin/bf2_client`, Python/AI libraries, DXVK, SDL3,
OpenAL, audio backends and the complete non-host dependency closure. The setup
runtime artifact contains Qt and its plugins/dependencies. Shared libraries must
be byte-identical. Engine inputs cannot replace Qt or setup files; runtime inputs
cannot replace the client, Python/AI or DXVK.

Use relative ELF runtime paths and materialize SONAME aliases as regular files.
Include dynamically loaded libraries as well as link-time dependencies. Do not
bundle libc or host GPU drivers, retain build-machine paths, or use
`-march=native` for a portable release.

## Integration sequence

1. Build the engine and runtime artifacts using the agreed SDK.
2. Check out a published, pinned setup commit containing Linux support. Earlier
   setup releases may only support macOS.
3. Validate the handoff before compiling setup:

   ```sh
   python3 linux/package.py check-inputs --engine /artifacts/engine --runtime /artifacts/runtime
   ```

4. Prepare the cached Cargo inputs and SDK development prefix, then invoke:

   ```sh
   export SETUP_CLIENT_ARTIFACT=/artifacts/engine
   export SETUP_RUNTIME_ARTIFACT=/artifacts/runtime
   export SETUP_SDK_PREFIX=/opt/openbf2-sdk
   export SETUP_BOOST_ROOT=/opt/boost-1.89.0
   linux/build-release.sh /output/OpenBF2-linux-x86_64.tar.gz /scratch/new-release
   ```

5. Keep AppImage and Flatpak wrappers downstream of the audited portable tree.
   Extend the engine release allowlist with the application assets and checksums
   while retaining existing server assets.
6. Complete the [remaining release acceptance](linux-validation.md#remaining-acceptance)
   before claiming supported-platform compatibility.

For local development, CMake's `SETUP_CLIENT_ARTIFACT` option imports an existing
artifact. `SETUP_CLIENT_ALLOW_DEVELOPMENT=ON` permits newer-libc builds without
release metadata and marks the result as non-distributable. It still enforces
the layout and relative dependency closure. Release entry points reject this mode.

## Runtime compatibility

The launcher runs the client in the selected writable game-data generation,
preserves the desktop/XDG environment, selects SDL3 for DXVK and adds the bundled
library directory only to the child's search path. It holds the installation
lock until the child exits. Application replacement must preserve profiles,
keys, custom mods and game generations.

This integration does not change network protocols, game-content formats,
receipt/journal formats, profile or key storage, dedicated-server interfaces,
or the macOS bundle contract. Native musl builds, Linux ARM64 releases, Gaming
Mode and automatic updates remain outside this distribution milestone.
