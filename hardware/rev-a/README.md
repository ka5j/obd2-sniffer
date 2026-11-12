# Revision A: working brief

Part of the [project documentation](../../docs/README.md). The folder's guide is [here](../README.md), and
[the circuit board](../../docs/design/circuit-board.md) explains why each block exists.

**Status: schematic in progress.** One sheet of five is drawn: USB and power. No board layout yet.

The schematic can be read without KiCad in [obd2-sniffer-rev-a.pdf](obd2-sniffer-rev-a.pdf), which is exported from
the KiCad files each time they change.

## In short

Revision A is the first board, built for bring-up and testing rather than to be small. It has room to probe, test points
on every important signal, and a debug header. Its mistakes get fixed in revision B, the finished plug-in device.

This page is the working sheet: the decisions, the parts and the datasheet each one was checked against, the pins, and
the checklist to complete before ordering. Everything on it can change, except the safety rules.

## Decisions

| Decision | Choice | Reason |
|---|---|---|
| Power | USB only. No connection to the car's 12 V (OBD2 pin 16) | The board only works with a laptop attached, and the laptop supplies 5 V. Removes the battery voltage from the board |
| CAN transceiver | One with a silent pin, not a standby pin | Silent mode keeps the receiver fully working with the transmitter off. A standby pin would stop normal receiving too |
| Silent-pin default | A 10 k resistor to 3.3 V on the board | The transceiver's internal resistor pulls the pin low, which selects "able to transmit". The board's resistor has to win, and does by a wide margin: the pin draws at most 10 µA |
| Silent signal | One signal for both transceivers | The self-test then needs the switch in OBD, which also tests the switch. One pin, one light |
| TX light | Switched by a transistor from the silent signal | The light can't load the signal, and can't be lit by firmware alone |
| Self-test connection | CAR/TEST jumpers on a three-position header | CAN2 can't reach the car connector in any position |
| Termination | CAN1: solder jumper, open by default. CAN2: fitted | The car's bus has its own. The self-test bus uses CAN2's |
| USB fuse | A chip fuse, not a resettable one | Lower resistance, so the transceivers' 5 V stays above their 4.5 V minimum |
| Car connector | 3 screw terminals: CANH, CANL, ground | Wiring can be checked. There is no terminal for pin 16 |

## Blocks and requirements

Every part on the board has to trace to a [requirement](../../docs/design/requirements.md).

| Block | Serves | Schematic drawn? |
|---|---|---|
| USB-C: power, fuse, data and ESD protection | PWR-5, LNK-3, USE-4 | Yes: `usb-power.kicad_sch` |
| 3.3 V regulator, PWR light | PWR-5, USE-3 | Yes: `usb-power.kicad_sch` |
| Microcontroller: crystal, decoupling, reset, BOOT0, SWD header | USE-4 | |
| CAN1 transceiver, ESD protection, termination solder jumper, car connector | CAN-1, CAN-6, PWR-8 | |
| CAN2 transceiver and CAR/TEST jumpers | CAN-7 | |
| LISTEN/OBD switch, silent signal, TX light | SAF-3, USE-3 | |
| BUS light | USE-3 | |
| Test points, debug serial header | Revision A only | |

## Parts

"Read" means the maker's datasheet was read for this design and the values below come from it.

| Part | Number | Why this one | Datasheet | Read? |
|---|---|---|---|---|
| Microcontroller | STM32F446RET6, LQFP64 | Same chip as the development board | [ST, DS10693 rev. 11](https://www.st.com/resource/en/datasheet/stm32f446re.pdf), with application notes AN4488 rev. 7, AN2606 rev. 71 and AN2867 rev. 25 | Yes |
| CAN transceiver, 2 of | TJA1051T/3, SO8 | Silent pin; 3.3 V logic supply; bus pins withstand -58 to +58 V; transmit timeout 0.3 to 5 ms; disconnects from the bus when unpowered (leakage at most 5 µA) | [NXP, rev. 9](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf) | Yes |
| CAN ESD protection | NUP2105L, SOT-23 | Working voltage 24 V and breakdown at least 26.2 V, so a short to a 16 V battery doesn't make it conduct; 30 kV contact ESD | [onsemi, rev. 12](https://www.onsemi.com/pdf/datasheet/nup2105l-d.pdf) | Yes |
| 3.3 V regulator | AP2112K-3.3, SOT-23-5 | 600 mA; input up to 6.5 V absolute; stable with 1 µF ceramic capacitors in and out | [Diodes, rev. 2-2](https://www.diodes.com/assets/Datasheets/AP2112.pdf) | Yes |
| USB ESD protection | USBLC6-2SC6, SOT-23-6 | Made for USB data pairs: at most 3.5 pF per line, 8 kV contact ESD, and it protects the 5 V line too | [ST, DS4260 rev. 7](https://www.st.com/resource/en/datasheet/usblc6-2.pdf) | Yes |
| USB-C socket | GCT USB4105-GF-A, 16 pins | USB 2.0 pins only; VBUS contacts rated 5 A together; 20,000 matings; a footprint ships with KiCad | [GCT](https://gct.co/files/drawings/usb4105.pdf) | Yes |
| USB fuse | Bourns SF-1206F100-2, 1 A fast, 1206 | 0.132 ohm typical, so about 35 mV lost at 250 mA; rated 63 V, interrupts 50 A | [Bourns](https://www.bourns.com/docs/product-datasheets/sf-1206f.pdf) | Yes |
| Resistors, capacitors, PWR light | 0603 (4.7 µF: 0805), capacitors X7R 16 V | Large enough to rework by hand on a test board | | |
| Crystal, switch, buttons, other lights, terminals | To choose | | | |

**Figures the design relies on, from the datasheets read**

| Figure | Value | Used for |
|---|---|---|
| TJA1051 silent pin | High is silent, low is normal. Internal pull-down, 1 to 10 µA at the pin | The board's pull-up and the switch |
| TJA1051 supply | 4.5 to 5.5 V. Below its undervoltage level (3.5 to 4.5 V) it disconnects from the bus | The 5 V budget from USB |
| TJA1051 supply current | At most 70 mA each while sending a dominant bit, 10 mA idle | The USB power budget |
| TJA1051 logic supply | 2.8 to 5.5 V | Runs from the 3.3 V rail |
| USB 5 V budget | 4.75 V at the laptop's port, less the cable, socket and fuse: about 4.6 V at the transceivers, against their 4.5 V minimum | Choosing a low-resistance fuse. On a weak port the transceivers disconnect from the bus, which is the safe direction |
| AP2112 heat | 184 °C/W in SOT-23-5 | (5 - 3.3) V at 150 mA is 0.26 W, about 47 °C of rise |
| STM32F446 core capacitor | LQFP64 has one VCAP pin: a single 4.7 µF ceramic, ESR under 1 ohm | The microcontroller sheet. Larger packages use two 2.2 µF instead |
| STM32F446 decoupling | 100 nF at each VDD pin plus one 4.7 µF; 100 nF plus 1 µF on VDDA; VBAT to VDD with 100 nF; 100 nF on NRST | The microcontroller sheet |
| STM32F446 crystal | 4 to 26 MHz. The crystal's critical gain must stay under 1 mA/V | Choosing the crystal and its load capacitors |
| STM32F446 USB bootloader | Needs a crystal that is a whole number of MHz, and prefers a low one such as 8 MHz. Entered with BOOT0 high and PB2 low. No pull-up resistor needed on the data lines | The 8 MHz crystal, the BOOT0 button and a pull-down on PB2 |
| STM32F446 bootloader pins | In bootloader mode the chip drives or pulls PA4 to PA10, PB5, PB6, PB9 to PB15, PC7 and PC9 to PC11 | The silent-signal pin must not be one of these, or the bootloader could enable the transmitter |

## Microcontroller pins

Pin numbers are for LQFP64. Each was checked in ST's datasheet (table 10) and matches KiCad's symbol for the chip.

| Function | Pin (LQFP64 number) | Where it comes from |
|---|---|---|
| CAN1 receive, transmit | PB8 (61), PB9 (62) | Used by the firmware today ([`board.h`](../../firmware/system/board.h)) |
| Debug serial TX, RX (USART2) | PA2 (16), PA3 (17) | Used by the firmware today ([`board.h`](../../firmware/system/board.h)) |
| CAN2 receive, transmit | PB12 (33), PB13 (34) | Reserved ([pin assignments](../../docs/design/hardware.md#pin-assignments)) |
| USB data D-, D+ | PA11 (44), PA12 (45) | Fixed by the chip's USB peripheral |
| SWD debug SWDIO, SWCLK | PA13 (46), PA14 (49) | Fixed by the chip |
| Crystal in, out | PH0 (5), PH1 (6) | Fixed by the chip |
| BOOT0, BOOT1, reset | BOOT0 (60), PB2 (28), NRST (7) | Fixed by the chip |
| Silent-signal control | PC6 (37) | Chosen: the bootloader leaves it alone, so it floats and the board's pull-up keeps the transceivers silent |
| BUS light | PC8 (39) | Chosen: not used by the bootloader |

## What the firmware will need for this board

None of it is built yet.

- **Crystal:** the firmware expects an 8 MHz clock from the development board's programmer. An 8 MHz crystal needs one
  setting changed ([clock tree](../../docs/design/build-and-boot.md#clock-tree)).
- **Pins:** a board definition for revision A beside the development board's.
- **New drivers:** native USB, CAN2, the silent-signal control and the BUS light.
- **No user button:** the development board's button goes away, along with its event.

## Before ordering

**The design**
- [ ] ERC and DRC pass, and every exclusion has a written reason ([design checks](../README.md#design-checks))
- [ ] Every part in the table above has a reason and a read datasheet, and is in stock
- [ ] Every microcontroller pin in the schematic matches the pin table above
- [ ] Every footprint checked against its datasheet, and the board checked in KiCad's 3D view
- [ ] Reviewed by someone outside the project

**The safety rules** ([safety.md](../../docs/design/safety.md))
- [ ] No net on the board connects to OBD2 pin 16, and the car connector has only CANH, CANL and ground
- [ ] The silent signal is pulled to silent on the board, so a reset, crash or blank chip means silent
- [ ] In LISTEN, the switch stops the firmware enabling the transmitter, however the firmware behaves
- [ ] The TX light is switched by the silent signal itself, not by firmware
- [ ] CAN1's transceiver has a dominant timeout, and its bus pins withstand a short to the car's battery
- [ ] CAN1's termination is open by default
- [ ] No position of the CAR/TEST jumpers connects CAN2 to the car connector

**The rule for ordering**
- [ ] [The conditions for ordering boards](../README.md#before-ordering-any-boards) are met

## Errata

Mistakes found on the built board go in `ERRATA.md` in this folder, so revision B can fix them.
