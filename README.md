# bekant_lin_master

ESPHome external component that turns an ESP32 plus a LIN transceiver into the LIN master of an
IKEA Bekant sit/stand desk, replacing the original up/down switch box and exposing the desk to
Home Assistant.

> Status: running on the desk (Click VBB on 12 V). Keep a hand near the power plug on the first
> runs.

## How it works

The Bekant switch box is a LIN master that polls both legs and tells them where to go. This
component does the same from an ESP32:

- `components/bekant/lin_bus.*` - LIN master on an ESP-IDF UART (break, sync, PID, checksums,
  echo check).
- `components/bekant/bekant.*` - the desk: start-up handshake, the 5 ms bus schedule and the
  controller state machine, ported from [Megadesk](https://github.com/gcormier/megadesk). The bus
  runs in its own FreeRTOS task; the ESPHome main loop only exchanges atomics with it.
- Entity platforms: `cover`, `sensor`, `number`, `button`, `text_sensor`.

Protocol details: [docs/protocol.md](docs/protocol.md). Hardware, 12 V supply and wiring:
[docs/hardware.md](docs/hardware.md).

## Hardware

ESP32-S3-DevKitC-1 (or any ESP32) + MikroE MCP2003B Click on the cable of the original switch box
(red VBAT 29 V, white GND, blue LIN).

> **The LIN bus runs at 12 V, not at the desk's 29 V.** Feed the Click's VBB with 12 V from a
> DC-DC buck converter (e.g. RECOM R-78HB12-0.5). Never connect the red 29 V wire directly to VBB.

The Click itself needs no modifications. See [docs/hardware.md](docs/hardware.md).

## Build

```bash
pip install esphome
cp secrets.yaml.example secrets.yaml   # fill in
esphome run bekant-desk.yaml
```

## Entities

| Platform | Key | What |
|----------|-----|------|
| cover | - | Desk up/down/stop and position (0 % = lowest, 100 % = highest allowed) |
| sensor | `height` | Height in cm (calibrated) |
| sensor | `raw_height` | Encoder value of leg A (diagnostic) |
| sensor | `drift` | Encoder difference between the legs (diagnostic) |
| number | `target_height` | Height slider in cm; follows the desk when it stops |
| button | `recalibrate` | Motor recalibration: drives slowly to the bottom end stop and re-zeroes the legs (see below) |
| text_sensor | `status` | starting / initialising legs / idle / moving up / moving down / recalibrating / faults (incl. "recalibration aborted") |

From lambdas (`id(desk)`):

```cpp
id(desk).move_to_cm(110.0);
id(desk).move_to_raw(4000);
id(desk).move_to_position(0.5);
id(desk).stop();
id(desk).recalibrate();
id(desk).set_button(1);   // +1 up held, -1 down held, 0 released
id(desk).get_height_cm(); id(desk).is_moving(); id(desk).is_online();
```

## Calibration

The legs report encoder counts. To get centimetres:

1. Drive the desk low, measure the height with a tape, note `Height (raw)`.
2. Drive it high, measure and note again.
3. Put both pairs in `calibration:` (`raw_low`/`cm_low`, `raw_high`/`cm_high`).

`min_height_raw` / `max_height_raw` keep the desk away from obstacles (range 299-6640).

## Safety

- With the switch box gone there is no physical stop button unless you wire one (see the
  commented `binary_sensor` in `bekant-desk.yaml`). Cover "stop" in Home Assistant works too.
- The legs are only driven while both answer every cycle. After 10 missed cycles the component
  drops to idle and re-runs the start-up handshake.
- When the legs drift more than `max_drift` counts apart (default 200) the desk refuses to move
  until it has been recalibrated.
- Recalibration drives the desk to its lowest position. Clear the space under the desk first.

## Credits and license

The bus schedule, init sequence and state machine come from
[Megadesk](https://github.com/gcormier/megadesk) by Greg Cormier (GPL-3.0), with protocol notes
from [trainman419/bekant](https://github.com/trainman419/bekant) and
[IKEA-Hackant](https://github.com/robin7331/IKEA-Hackant). This project is therefore GPL-3.0 too,
see [LICENSE](LICENSE).
