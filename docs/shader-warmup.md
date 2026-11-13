# Shader warm-up step

The native client compiles a map's shaders into its `FxNative` cache the first
time the map loads, which takes minutes on a cold cache. The macOS client has a
non-retail switch, `+warmupLevel <level>`, that hosts the level locally in a
normal window, builds every shader of the level load, presses JOIN GAME itself
so the first world frame's effects are built too, waits until the background
builds have been quiet for ten seconds and exits. The assistant offers one
such run per shader set after voice setup, and again behind "Prepare Shaders…"
on the Ready page. The step can be skipped; the game then compiles at first load.

## Runs

| run | level | mod | when |
|---|---|---|---|
| Battlefield 2 maps | `strike_at_karkand` | `mods/bf2` | always (covers the bf2 and Special Forces day maps) |
| Special Forces night maps | `devils_perch` | `mods/xpack` | Deluxe installations (covers the three night maps) |

Each run is `bf2_client +modPath <mod> +warmupLevel <level>` in the verified
game-data generation with the same environment as Play. If
`Profiles/global.con` names a default user, `+playerName <name>` is added so
that profile's video settings select the shader set; otherwise the client's
defaults apply. Changing the video quality later means the new set compiles at
first load, or the step can be re-run. The runs are sequential, hold
`Setup/Install.lock` like Play, and Cancel terminates the current run.

## Progress and outcome

The client prints `warmup: start <level>`, `warmup: profile <name>`,
`warmup: effects N/M` (at most once a second), `warmup: level loaded`,
`warmup: player joined`, `warmup: join game`, `warmup: world frame` and
`warmup: done effects N/M failed F <seconds> s` on stdout. The assistant reads
them through a pipe and shows "N of M effects · <stage>" with a progress bar.

Exit status: 0 done; 2 the level could not start; 3 timeout
(`BF2_WARMUP_TIMEOUT`, default 3600 s); 4 done with some native effect build
failures. Non-zero outcomes are shown per run; the step still completes.

Measured by the engine team on an i7-8569U: Karkand about 70 s, Devil's Perch
130 to 225 s. The step is skipped in the UI smoke mode and all tests; the
argument construction, default-user lookup, line parsing and outcomes are
checked headlessly in `tests/setup_session.cpp`.
