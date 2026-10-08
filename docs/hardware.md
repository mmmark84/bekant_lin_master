# Hardware

## The desk

IKEA Bekant sit/stand desk, version with a two-button switch box and a safety key.

- Two legs, each with a motor, an encoder and its own LIN slave. Each leg has two identical
  3-pin TE VAL-U-LOK sockets, so the power supply, the inter-leg cable and the switch box are
  daisy-chained on one 3-wire bus.
- The switch box is the LIN master (PIC16LF1938 + LIN transceiver). Without it, or with only one
  leg on the bus, nothing moves.
- Power supply: 29 V DC. Megadesk measured peaks of 35-37 V while the motors run.

Bus wires in the original switch-box cable:

| Wire  | Signal |
|-------|--------|
| red   | VBAT (29 V) |
| white | GND |
| blue  | LIN |

Check this with a meter before connecting. With the legs powered and no master, LIN also sits
close to VBAT (slave pull-ups). Load it with 10 kOhm to GND: LIN sags a lot, VBAT doesn't.

## Electronics

- NodeMCU ESP32 (any ESP32 board works), powered over USB while developing.
- MikroE MCP2003B Click (MIKROE-2227), modified as described below.

### MCP2003B Click modifications

The MCP2003B itself is rated 30 V continuous / 50 V absolute maximum, which suits the desk. Two
other parts on the Click are not:

| Part | What it is | Why it goes |
|------|------------|-------------|
| U1 (MCP1804) | 28 V LDO fed from VBB; only powers the 2.2 k pull-up on RXD | 28 V operating / 30 V abs max: dies on the desk's peaks |
| D3, D4 (marking `KVP`) | 27 V dual zeners (DZ23C27 / MMBZ27VCL type) | start conducting at ~26-28 V, i.e. at the desk's idle voltage |

Steps:

1. Remove U1.
2. Wire the former U1 pin 5 pad (VOUT) to the 3.3V pin of the mikroBUS header. On the board with
   the marking readable, pin 5 is the lower pad on the 2-pad side, next to R2. Verify with a
   continuity tester: pin 5 pad -> R2 -> MCP2003B pin 1 (RXD).
3. Remove D3 and D4.
4. Leave the L-PULL jumper in place (master pull-up, 1 k + diode from VBB to LIN).
5. Leave CS SEL on HI (transceiver always enabled). On "CS" you can drive it from a GPIO instead
   (`cs_pin` in the YAML).

After this only D1 (reverse polarity diode) and the MCP2003B hang on VBB. The RXD pull-up now
runs from the ESP32's 3.3 V (at most ~1.5 mA).

```
before:  VBB -- U1 (MCP1804) -- pin5 net --+-- C_out -- GND
                                           +-- R2 2.2k -- RXD (MCP2003B pin 1) -- TX pin

after:   ESP32 3V3 -- wire ---- pin5 net --+-- C_out -- GND
                                           +-- R2 2.2k -- RXD (MCP2003B pin 1) -- TX pin
```

### Wiring

| Click | NodeMCU ESP32 |
|-------|---------------|
| 3.3V (mikroBUS pin 7) | 3V3 |
| GND (pin 8/9) | GND |
| TX (pin 14, MCP2003B RXD out) | GPIO16 |
| RX (pin 13, MCP2003B TXD in) | GPIO17 |
| AN (pin 1, VREN divider) | **not connected** - can exceed 3.3 V at 29 V VBB |

Screw terminal: VBB = red, LIN = blue, GND = white.

First power-up: connect only 3V3 + GND from the ESP32, no desk. The Click's TX pin should read
~3.3 V. Then connect the desk.

### Later: standalone power

Feed the ESP32 from the desk with a buck converter rated well above 37 V input, e.g. RECOM
R-78HB5.0-0.5 (9-72 V in, 5 V 0.5 A) into the board's 5 V/VIN pin, with a Schottky diode in
series if USB may be connected at the same time. The AMS1117 on most dev boards (max ~15 V) and
an MP1584 (28 V) are not suitable.
