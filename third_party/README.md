# Third-party sources

`rtptool/` is an unmodified source subset of the owner's rtptool fork,
https://github.com/kr4uzi/rtptool, pinned to
`101433150a37001c74823659f56391723378e0b0` (branch `bf2-0x0320`, merged there
as pull request #1). Included: `src/`, `Cargo.toml`, `Cargo.lock` and the MIT
notice `rtptool/LICENSE`. The fork's README, format notes and tests are not
vendored; read them at that revision in the fork. The original upstream is
https://github.com/bwrsandman/rtptool.

This directory is a submodule-ready path: replace the snapshot with the same
commit from the fork, or configure CMake with
`-DSETUP_RTPTOOL=/absolute/path/to/a/checkout` of a revision that contains the
pinned 0x0320 API. CMake writes a build-local Cargo manifest; it never edits the
supplied checkout. There are no FetchContent, package installation, git update,
or network build steps.

The bridge is project code under `core/rtp-bridge/`, with a path dependency and
locked offline Cargo dependencies. `THIRD-PARTY-NOTICES.txt` retains their notices.
Their source is supplied by the already populated Cargo cache, not downloaded.
A hermetic release builder must separately preseed/vendor those locked crates.

zlib is a required system/build dependency (`find_package(ZLIB REQUIRED)`),
not a newly vendored copy. No unshield source is included: the format-9 reader
is the original prototype implementation, with its preserved licence notice.
