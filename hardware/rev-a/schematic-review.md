# Revision A: schematic review

Part of the [project documentation](../../docs/README.md). The folder's guide is [here](../README.md), and the
[revision A brief](README.md) holds the decisions, parts and pins this review checks.

## In short

The whole schematic was read back from its netlist, net by net, against the datasheets listed in the brief.

- **Result:** no wiring error found. ERC passes with no errors and no warnings.
- **One change made:** capacitor C1 on the 5 V rail went from 4.7 µF to 1 µF, so the charge the board takes from the
  USB port at plug-in stays inside the USB limit.
- **Not closed:** three datasheets are still unread (the LISTEN/OBD switch, the buttons, the screw terminal), and
  nobody outside the project has reviewed the schematic. The switch datasheet has to be read before board layout.

Everything here is reasoning from datasheets. Nothing has been built or measured.

## How it was checked

| Check | Tool | Result |
|---|---|---|
| Electrical rules | `kicad-cli sch erc --severity-all` | 0 errors, 0 warnings |
| Every net and every pin on it | Netlist exported with `kicad-cli sch export netlist`, read back against each part's datasheet pin table | No error found |
| Microcontroller pins | Netlist against the brief's pin table and [`board.h`](../../firmware/system/board.h) | All match; see below |
| Ratings | Each part's voltage, current and power against its datasheet | Inside limits; see below |

## Current on each supply

Worst-case figures from the datasheets, added up.

**3.3 V rail**

| Load | Current | Source |
|---|---|---|
| Microcontroller, 180 MHz, every peripheral on, 105 °C | 110 mA | ST DS10693, table 23 (maximum) |
| Three lights through 1 k each | 4.5 mA | 1.5 mA each at most |
| Two transceivers' logic supply | 1 mA | NXP: 500 µA each at most |
| Silent signal's pull-up, when driven low | 0.7 mA | 3.3 V across 4.7 k |
| **Total** | **about 120 mA** | The regulator is rated 600 mA |

The regulator then dissipates (5.25 - 3.3) V x 120 mA = 0.23 W, a rise of about 43 °C at 184 °C/W.

**5 V from USB**

| Case | Current | Limit |
|---|---|---|
| Just plugged in, before the laptop has configured the device: microcontroller on its 16 MHz internal clock, transceivers silent | about 32 mA | 100 mA (USB 2.0, unconfigured device) |
| Normal use, both transceivers sending a dominant bit at once: 120 mA + 2 x 70 mA | 260 mA | 500 mA (USB 2.0), fuse 1 A |
| Both bus outputs shorted while sending: 120 mA + 2 x 110 mA | 340 mA | 500 mA (USB 2.0), fuse 1 A |

At 260 mA the transceivers see about 4.59 V: 4.75 V at the port, less 0.13 V in the cable and 0.03 V in the fuse. Their
minimum is 4.5 V. On a weaker port they disconnect from the bus, which is the safe direction.

## Charge taken at plug-in

USB 2.0 lets a device present at most 10 µF to the port when it is plugged in, which is 50 µC at 5 V. That figure is
from the USB 2.0 specification (section 7.2.4.1) as remembered; the specification was not re-read for this review.

| Capacitance | Before | After |
|---|---|---|
| On the 5 V rail: C1, C2, C16, C18 | 5.9 µF, 29.5 µC | 2.2 µF, 11 µC |
| Behind the regulator, at 3.3 V: 7.5 µF | 24.8 µC | 24.8 µC |
| The microcontroller's core capacitor, at about 1.25 V: 4.7 µF | 5.9 µC | 5.9 µC |
| **Total, if all of it charges at once** | **60 µC** | **42 µC** |

Counting everything behind the regulator is pessimistic, because the regulator slows that charge. Counted that way the
first design was over the limit, so C1 was reduced from 4.7 µF to 1 µF. The 5 V rail keeps 2.2 µF, with 100 nF at each
transceiver. The actual current at plug-in gets measured on the built board.

## Microcontroller pins

| Signal | Schematic | Brief | Firmware today |
|---|---|---|---|
| CAN1 receive, transmit | PB8 (61), PB9 (62) | Same | Same, in `board.h` |
| Debug serial TX, RX | PA2 (16), PA3 (17) | Same | Same, in `board.h` |
| CAN2 receive, transmit | PB12 (33), PB13 (34) | Same | Not built |
| USB D-, D+ | PA11 (44), PA12 (45) | Same | Not built |
| SWDIO, SWCLK, SWO | PA13 (46), PA14 (49), PB3 (55) | Same | Fixed by the chip |
| Crystal | PH0 (5), PH1 (6) | Same | Clock setting to change |
| BOOT0, BOOT1, reset | 60, PB2 (28), 7 | Same | Fixed by the chip |
| Silent-signal control | PC6 (37) | Same | Not built |
| BUS light | PC8 (39) | Same | Not built |

The development board's light (PA5) and button (PC13) are not on this board. Both pins are unconnected, so the
firmware needs a board definition for revision A before it runs here.

## Ratings

| Part | Stress in this circuit | Rating |
|---|---|---|
| Capacitors on 5 V and 3.3 V | 5.25 V at most | 16 V |
| Termination resistors R9, R10 (0805) | 3 V across 120 ohm: 75 mW | 125 mW |
| Fuse F1 | 5.25 V, 340 mA worst case | 63 V, 1 A |
| Regulator U2 | 5.25 V in, 120 mA, 0.23 W | 6.5 V absolute, 600 mA |
| Transceivers' bus pins | A short to a 16 V battery | -58 to +58 V |
| ESD protection D4 | A short to a 16 V battery | Does not conduct below 24 V |
| TX light transistor Q1 | 3.3 V gate to source, 1.5 mA | 20 V, 130 mA |
| PC6 driving the silent signal low | 0.7 mA | The pin holds under 0.4 V at 8 mA, and the transceiver needs under 0.99 V |

## Findings

| Finding | Action |
|---|---|
| Plug-in charge could exceed the USB limit | **Changed:** C1 is 1 µF |
| In TEST the self-test bus has one 120 ohm resistor, not two | None. The bus is a few centimetres long. Closing the solder jumper gives two for a desk test |
| A serial adapter on the debug header can feed the 3.3 V rail through PA3 while USB is unplugged | None. The transceivers have no 5 V then, so they stay disconnected from the bus |
| The microcontroller's analog supply has no filter | None. The board doesn't use the analog converter |
| The crystal's drive level (100 µW at most) can't be calculated from the datasheets alone | Check the clock on the built board |
| The lights have no part number yet | Choose at ordering. Each gets 1.2 to 1.5 mA |
| USB firmware must not wait for a VBUS signal on PA9, and must declare the current it draws | Added to the firmware list in the brief |

## Still open

| Item | Blocks | Needs |
|---|---|---|
| LISTEN/OBD switch datasheet. The schematic assumes pin 2 is the common pin and that the switch breaks one contact before making the other. KiCad's footprint for it has pads 1, 2 and 3 in a row, which fits, but is not proof | Board layout | The C&K JS series datasheet |
| Button and screw terminal datasheets | Footprint checks at layout | The C&K PTS645 and Phoenix Contact MKDS 1,5/3-5,08 datasheets |
| SWD header pin order, meant to match the development board's programmer | Nothing: each pin is labelled on the board and wired singly | ST's UM1724 to confirm |
| Parts in stock | Ordering | A check at the time of ordering |
| Review by someone outside the project | Ordering | This page, the brief and [the schematic PDF](obd2-sniffer-rev-a.pdf) are the pack to post |
