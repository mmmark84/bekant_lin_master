# Bekant LIN protocol

Collected from [Megadesk](https://github.com/gcormier/megadesk) (`code/src/megadesk.cpp`,
`Reverse Engineering/`) and [trainman419/bekant](https://github.com/trainman419/bekant). This
component replays what Megadesk sends; Megadesk in turn replays the original controller.

## Physical layer

- LIN, 19200 baud, 8N1. Break: Megadesk uses 15 bit times, this component 18 (0x00 at 9600 baud).
- Checksum: enhanced (PID + data) for all frames except the diagnostic frames 0x3C/0x3D, which use
  the classic checksum (data only). Carry is added back in, result inverted.
- The switch box is the master, the legs are slaves.

| ID   | PID  | Direction | Data |
|------|------|-----------|------|
| 0x11 | 0x11 | master | `00 00 00` |
| 0x08 | 0x08 | leg A  | encoder LSB, encoder MSB, status |
| 0x09 | 0x49 | leg B  | encoder LSB, encoder MSB, status |
| 0x10 | 0x50 | master | none (Megadesk still sends a checksum byte) - purpose unknown |
| 0x01 | 0xC1 | master | none (idem) - purpose unknown |
| 0x12 | 0x92 | master | target LSB, target MSB, command |
| 0x3C | 0x3C | master | 8-byte diagnostic request (init only) |
| 0x3D | 0x7D | leg    | 8-byte diagnostic response (init only) |

Checksum check against a capture of the original controller (`Rising.csv`):
`0x12: 143 1 134 -> 86` and `0x09: 185 1 2 -> 249` both match.

## Schedule

One burst, frames 5 ms apart:

```
0x11 [00 00 00] -> 0x09 (leg B answers) -> 0x08 (leg A answers) -> 0x10 x6 -> 0x01 -> 0x12 [cmd]
```

The original controller pauses ~150 ms between bursts, Megadesk 50 ms (`cycle_gap`).

## Leg status (byte 3 of 0x08/0x09)

| Value | Meaning |
|-------|---------|
| 0, 37, 96 (0x60) | idle |
| 2 | moving (fast) |
| 3 | moving (fine adjust) |
| 1 | at the bottom during recalibration |
| 224 | first position report after power-up |

Higher encoder value = higher desk. Usable range 299-6640 (end stops at ~162 / ~6777).

## Commands (byte 3 of 0x12)

| Value | Name | Target bytes |
|-------|------|--------------|
| 0xFC | idle | leg A position |
| 0xC4 | pre-move (one cycle before moving) | leg A position |
| 0x86 | raise | position of the lower leg |
| 0x85 | lower | position of the higher leg |
| 0x87 | fine (3 cycles while stopping) | requested target |
| 0x84 | finish (until leg A reports idle) | lower/higher leg as above |
| 0xBD | recalibrate (drive to bottom) | 0 |
| 0xBC | end recalibration | 99 |
| `F6 FF BF` | whole frame, sent once after the init sequence | - |

Aiming both legs at the lagging leg is what keeps the desk level.

## Controller state machine

```
OFF --(up/down requested, both legs idle)--> STARTING [C4]
STARTING --> UP [86] / DOWN [85] / OFF
UP   --(no longer requested, or max reached)--> STOPPING1
DOWN --(no longer requested, or min reached)--> STOPPING1
STOPPING1..3 [87] --> STOPPING4 [84] --(leg A idle)--> OFF
STARTING_RECAL [C4] --> RECAL [BD, target 0] --(both legs status 1, enc <= 99)--> END_RECAL [BC, 99] --> OFF
```

The original controller and Megadesk ignore all input during recalibration and have no way out of
RECAL other than a power cycle. This component also drops from STARTING_RECAL/RECAL straight to OFF
(idle commands, no BC) on stop, a button press or after 2 minutes. How the legs react to a
recalibration cut short is not known.

Up/down is requested while the target is more than 137 counts (hysteresis) away. A held button
keeps the target 159 counts ahead of the current position.

## Init sequence

The legs are identical, so the master assigns their node addresses at start-up with diagnostic
frames. Each step: 0x3C with `a b c d FF FF FF FF`, then read 0x3D; 10 ms between frames.

| Step | a | b c d | Notes |
|------|---|-------|-------|
| 0-1  | 255 | 7 255 255 | no answer expected |
| 2    | 255 | 1 7 255 | |
| 3    | 208 | 2 7 255 | |
| 4    | n | 2 7 255 | n = 0, 1, ... until a leg answers: leg A |
| 5-9  | n | 6 9 0, 6 12 0, 6 13 0, 6 10 0, 6 11 0 | |
| 10   | n | 4 0 0 | |
| 11   | n | 2 0 0 | n continues counting until the second leg answers: leg B |
| 12-16| n | 6 9 0, 6 12 0, 6 13 0, 6 10 0, 6 11 0 | |
| 17   | n | 4 1 0 | |
| 18   | n | 2 1 0 | sweep n up to 8 |
| 19   | 208 | 1 7 0 | |
| 20   | 208 | 2 7 0 | |

Then, 15 ms later, `0x12: F6 FF BF`. Megadesk reports that about 1 in 20 handshakes fails and a
power cycle fixes it; this component re-runs the sequence when the legs stop answering.
