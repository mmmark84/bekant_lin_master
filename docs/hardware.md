# Hardware

## The desk

IKEA Bekant sit/stand desk, version with a two-button switch box and a safety key.

- Two legs, each with a motor, an encoder and its own LIN slave. Each leg has two identical
  3-pin TE VAL-U-LOK sockets, so the power supply, the inter-leg cable and the switch box are
  daisy-chained on one 3-wire bus.
- The switch box is the LIN master (PIC16LF1938 + LIN transceiver). Without it, or with only one
  leg on the bus, nothing moves.
- Power supply: 29 V DC. Megadesk measured peaks of 35-37 V while the motors run.
- The LIN bus does **not** run at the supply voltage. It runs at **12 V** (recessive level
  ~12 V, measured with a scope on the desk).

Bus wires in the original switch-box cable:

| Wire  | Signal |
|-------|--------|
| red   | VBAT, 29 V supply (not the LIN level) |
| white | GND |
| blue  | LIN, 12 V levels |

Check this before connecting: red reads ~29 V against white, blue idles around 12 V. Use a scope
on LIN if you have one.

## Electronics

- ESP32-S3-DevKitC-1 (any ESP32 board works), powered over USB while developing.
- MikroE MCP2003B Click (MIKROE-2227), unmodified.
- DC-DC buck converter, desk 29 V -> 12 V, for the Click's VBB.

### VBB: 12 V, never the desk's 29 V

> **Do not connect the red 29 V wire directly to VBB on the Click.** It does not work and it can
> damage the Click or the legs.

The MCP2003B takes its bus levels from VBB. The master pull-up on the Click (L-PULL: R5 1k + diode
from VBB to LBUS) pulls LIN up to VBB, and the receiver thresholds are fractions of VBB (dominant
below ~0.4 x VBB, recessive above ~0.6 x VBB). With 29 V on VBB the Click pulls a bus that the
legs run at 12 V towards 29 V, and its thresholds no longer match the legs' 12 V levels. On the
desk this did not work at all; with 12 V on VBB it does.

So VBB gets 12 V from a buck converter fed by the desk's 29 V:

```
desk red (29 V)  ---- DC-DC IN+    DC-DC OUT+ (12 V) ---- Click VBB
desk white (GND) --+- DC-DC IN-/OUT- (GND)
                   +------------------------------------- Click GND
desk blue (LIN)  ---------------------------------------- Click LIN
```

The converter's input must be rated well above the desk's 35-37 V peaks, e.g. RECOM
R-78HB12-0.5 (17-72 V in, 12 V 0.5 A, 78xx pinout). The Click needs far less than 0.5 A. An
MP1584 module (28 V max input) is not suitable. All grounds (desk, converter, Click, ESP32) are
common.

### MCP2003B Click: no modifications needed

Schematic: [MCP2003B click schematic v100](https://download.mikroe.com/documents/add-on-boards/click/mcp2003b/mcp2003b-click-schematic-v100.pdf).

With 12 V on VBB every part on the Click is within its rating: the MCP2003B (30 V), U1 (MCP1804
LDO, 28 V) and D3/D4 (MMBZ27VC, 27 V zeners). An earlier version of this document said to remove
U1, D3 and D4 so the Click could take the desk's 29 V on VBB. That is not needed, and 29 V on VBB
is wrong in the first place (see above).

Jumpers:

- Leave the L-PULL jumper (JMPR, 0R) in place: master pull-up R5 1k + BAV99 from VBB to LBUS.
- Leave CS SEL on HI (CS pulled to VREG through R6). On "LOW"/CS you drive it from a GPIO instead
  (`cs_pin` in the YAML).

Note: U1's output (VREG) is wired straight to the mikroBUS +3.3V pin. Besides the host's 3.3 V it
feeds the RXD pull-up (R2 2k2), the CS pull-up (R6 10k), the PWR LED and C2. On an unmodified Click
U1 therefore runs in parallel with the ESP32's own 3.3 V regulator. If you'd rather have the ESP32
alone feed the 3.3 V side, removing U1 is optional; nothing else is needed then.

### Wiring

| Click | ESP32-S3-DevKitC-1 | NodeMCU ESP32 |
|-------|--------------------|---------------|
| 3.3V (mikroBUS pin 7) | 3V3 | 3V3 |
| GND (pin 8/9) | GND | GND |
| TX (pin 14, MCP2003B RXD out) | GPIO18 | GPIO16 |
| RX (pin 13, MCP2003B TXD in) | GPIO17 | GPIO17 |
| AN (pin 1, VREN via R3 47k / R4 10k) | **not connected** | **not connected** |

AN carries ~2 V at 12 V VBB; the component doesn't use it. On the ESP32-S3, GPIO17/18 avoid the
strapping pins (0, 3, 45, 46), native USB (19/20), UART0 (43/44) and the octal PSRAM pins (35-37).

Screw terminal: VBB = 12 V from the DC-DC converter (**not** the red wire), LIN = blue,
GND = white + converter GND.

First power-up:

1. Connect only 3V3 + GND from the ESP32, no desk. The Click's TX pin should read ~3.3 V.
2. Power the DC-DC converter from the desk and measure its output: 12 V.
3. Connect the converter output to VBB, then LIN.

### Later: standalone power

Feed the ESP32 from the desk with a second buck converter, e.g. RECOM R-78HB5.0-0.5 (9-72 V in,
5 V 0.5 A), from the 29 V or the 12 V rail into the board's 5 V/VIN pin, with a Schottky diode in
series if USB may be connected at the same time. Don't put 12 V or 29 V on 5 V/VIN directly: the
AMS1117 on most dev boards (max ~15 V) can't take 29 V and would have to burn off ~9 V from 12 V.
