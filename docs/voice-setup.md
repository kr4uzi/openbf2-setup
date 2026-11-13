# Voice setup step

Retail Battlefield 2 ran `BF2VoiceSetup.exe` ("BF2 BattleCommo") at the end of
installation: a microphone level meter, a capture volume slider, an activation
threshold slider and a microphone boost checkbox. The setup assistant has the
same step after a successful installation and behind the "Voice Setup…" button
on the Ready page. It builds its own UI; no retail tool is run or reconstructed.

## The one file

Retail's tool writes no registry values and no profile files. It writes one
file, and so does the assistant:

```
~/Library/Application Support/OpenBF2/Profiles/BattleCommoDefaultUserSettings.con
```

`Profiles/` is created if missing, mode 0700. The file is written privately
(`O_CREAT|O_EXCL`, 0600, fsync) and published with the platform's atomic
replace under `Setup/Install.lock`, like `Registry.con`. Symlinks anywhere in
the path are rejected.

Four plain values, one per line, in `std::ostream` default formatting
(precision 6). They are not console commands.

| line | meaning | type | range | default | assistant source |
|---|---|---|---|---|---|
| 1 | VoIP playback volume | float | 0-1 | 1 | carried through from an existing file, else 1 (retail has no slider) |
| 2 | VoIP capture volume | float | 0-1 | 1 | "Capture volume" slider |
| 3 | capture threshold | float | 0-1 | 0.1 | "Activation threshold" slider |
| 4 | microphone boost | `0`/`1` | | 0 | "Microphone boost" checkbox |

Example with defaults: `1\n1\n0.1\n0\n`.

## Timing

The client reads this file only when it creates its first "Default" profile and
copies the values into that profile's `Audio.con`; from then on the profile's
own settings win, and the game's audio options write the file back. A step
that runs before the first launch therefore matches retail exactly. The
assistant never edits existing profiles. When profile directories already
exist the step says so and still writes the file, which seeds future profiles.

Reopening the step pre-fills the controls from the existing file; a missing,
partial or unparsable file yields the defaults, value by value, like the
client's reader.

## Microphone

The step checks the microphone authorization. If undetermined it offers
"Allow Microphone", which triggers the system prompt; if denied it offers
"Open System Settings" (Privacy & Security › Microphone). Once authorized an
`AVAudioEngine` input tap drives the 0-1 peak meter, scaled by the capture
volume slider because the client applies that volume itself. The threshold is
drawn as a marker on the meter and the bar turns green above it. Without an
input device the step still saves.

The assistant does not change the system input volume or any mixer boost; the
values are stored for the client. The bundle's `Info.plist` carries
`NSMicrophoneUsageDescription`; the game client runs as a child of the app
bundle and inherits the grant. Under a hardened-runtime release signature the
app needs the `com.apple.security.device.audio-input` entitlement
(see [distribution.md](distribution.md)).

The UI smoke mode and all tests never touch the microphone or its prompt: the
meter is fed a synthetic level and the file format is checked headlessly
(`tests/setup_session.cpp`).
