# Revision A: working brief

Part of the [project documentation](../../docs/README.md). The folder's guide is [here](../README.md), and
[the circuit board](../../docs/design/circuit-board.md) explains why each block exists.

**Status: schematic drawn and checked, with items still open.** All three sheets are drawn, and the
[schematic review](schematic-review.md) found no wiring error. Three datasheets are still unread and no one outside the
project has reviewed it. No board layout yet.

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
| Silent-pin default | A 4.7 k resistor to 3.3 V on the board | The transceiver's internal resistor pulls the pin low, which selects "able to transmit". The board's resistor has to win, also against the microcontroller's own 30 to 50 k pull-down if firmware turned it on by mistake: the signal then still sits at 2.8 V, above the 2.31 V the transceiver needs |
| Silent signal | One signal for both transceivers | The self-test then needs the switch in OBD, which also tests the switch. One pin, one light |
| TX light | Switched by a transistor from the silent signal, through a 100 k gate resistor | The light can't load the signal, and can't be lit by firmware alone. The resistor keeps a short inside the transistor from pulling the signal low |
| Self-test connection | CAR/TEST jumpers: two 3-pin headers, one for each bus wire, placed apart | CAN2 can't reach the car connector in any position. One 2 x 3 block was rejected: a jumper cap put on sideways would short the car's two CAN wires together |
| Termination | CAN1: solder jumper, open by default. CAN2: fitted | The car's bus has its own. The self-test bus uses CAN2's |
| USB fuse | A chip fuse, not a resettable one | Lower resistance, so the transceivers' 5 V stays above their 4.5 V minimum |
| Car connector | 3 screw terminals: CANH, CANL, ground | Wiring can be checked. There is no terminal for pin 16 |

## Blocks and requirements

Every part on the board has to trace to a [requirement](../../docs/design/requirements.md).

| Block | Serves | Schematic drawn? |
|---|---|---|
| USB-C: power, fuse, data and ESD protection | PWR-5, LNK-3, USE-4 | Yes: `usb-power.kicad_sch` |
| 3.3 V regulator, PWR light | PWR-5, USE-3 | Yes: `usb-power.kicad_sch` |
| Microcontroller: crystal, decoupling, reset, BOOT0, SWD header | USE-4 | Yes: `microcontroller.kicad_sch` |
| CAN1 transceiver, ESD protection, termination solder jumper, car connector | CAN-1, CAN-6, PWR-8 | Yes: `can.kicad_sch` |
| CAN2 transceiver and CAR/TEST jumpers | CAN-7 | Yes: `can.kicad_sch` |
| LISTEN/OBD switch, silent signal, TX light | SAF-3, USE-3 | Yes: `can.kicad_sch` |
| BUS light | USE-3 | Yes: `microcontroller.kicad_sch` |
| Test points, debug serial header | Revision A only | Yes: on all three sheets |

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
| Crystal | ECS ECS-80-10-30B-TR: 8 MHz, 10 pF load, 5.0 x 3.2 mm | At most 100 ohm, where 3.2 x 2.5 mm crystals at 8 MHz reach 400 ohm; pads 2 and 4 are ground | [ECS](https://ecsxtal.com/store/pdf/ECX-53B.pdf) | Yes |
| Reset and boot buttons | C&K PTS645 type, 6 mm | A footprint ships with KiCad | | Not yet: read when the footprint is checked at layout |
| TX light transistor | onsemi BSS84, SOT-23 | P-channel; gate leakage at most 10 nA, so it doesn't load the silent signal; turns on between 0.8 and 2.0 V below 3.3 V | [onsemi, rev. 5](https://www.onsemi.com/pdf/datasheet/bss84-d.pdf) | Yes |
| LISTEN/OBD switch | C&K JS102011SAQN slide switch | Two positions; a footprint ships with KiCad | | Not yet. The schematic assumes pin 2 is the common pin and that it breaks one contact before making the other |
| Car connector | Phoenix Contact MKDS 1,5/3-5,08, 3 screw terminals | A footprint ships with KiCad | | Not yet |
| CAR/TEST and other headers | 2.54 mm pin headers, with two jumper caps | | | |

**Figures the design relies on, from the datasheets read**

| Figure | Value | Used for |
|---|---|---|
| TJA1051 silent pin | High is silent, low is normal. Internal pull-down, 1 to 10 µA at the pin | The board's pull-up and the switch |
| TJA1051 supply | 4.5 to 5.5 V. Below its undervoltage level (3.5 to 4.5 V) it disconnects from the bus | The 5 V budget from USB |
| TJA1051 supply current | At most 70 mA each while sending a dominant bit, 10 mA idle | The USB power budget |
| TJA1051 logic supply | 2.8 to 5.5 V | Runs from the 3.3 V rail |
| USB 5 V budget | 4.75 V at the laptop's port, less the cable, socket and fuse: about 4.6 V at the transceivers, against their 4.5 V minimum | Choosing a low-resistance fuse. On a weak port the transceivers disconnect from the bus, which is the safe direction |
| TX light against the silent pin | The transceiver is certainly silent above 2.31 V and certainly able to transmit below 0.99 V. The light is certainly on below 1.3 V | Between about 1.3 and 2.3 V the board could be able to transmit with the light dark. The signal is driven to 0 or 3.3 V, and no single fault found leaves it in between (fault table in the pull request that added the CAN sheet) |
| AP2112 heat | 184 °C/W in SOT-23-5 | (5.25 - 3.3) V at the worst-case 120 mA is 0.23 W, about 43 °C of rise |
| Charge taken at plug-in | About 42 µC if every capacitor on the board charges at once, against 50 µC (10 µF at 5 V) allowed by USB 2.0 | C1 on the 5 V rail is 1 µF, not 4.7 µF ([schematic review](schematic-review.md#charge-taken-at-plug-in)) |
| STM32F446 core capacitor | LQFP64 has one VCAP pin: a single 4.7 µF ceramic, ESR under 1 ohm | The microcontroller sheet. Larger packages use two 2.2 µF instead |
| STM32F446 decoupling | 100 nF at each VDD pin plus one 4.7 µF; 100 nF plus 1 µF on VDDA; VBAT to VDD with 100 nF; 100 nF on NRST | The microcontroller sheet |
| STM32F446 crystal | 4 to 26 MHz. The crystal's critical gain must stay under 1 mA/V | Choosing the crystal and its load capacitors |
| Crystal start-up margin | Critical gain 4 x 100 ohm x (2 pi x 8 MHz)^2 x (5 pF + 10 pF)^2 = 0.23 mA/V, against the chip's 1 mA/V limit: a margin of about 22, where ST asks for 5 or more | Choosing the crystal. An 18 pF crystal of 400 ohm would have failed this check |
| Crystal load capacitors | 2 x (10 pF load - 5 pF stray) = 10 pF each. The stray figure is an estimate | Checked on the built board by measuring the clock |
| STM32F446 USB bootloader | Needs a crystal that is a whole number of MHz, and prefers a low one such as 8 MHz. Entered with BOOT0 high and PB2 low. No pull-up resistor needed on the data lines | The 8 MHz crystal, the BOOT0 button and a pull-down on PB2 |
| STM32F446 bootloader pins | In bootloader mode the chip drives or pulls PA4 to PA10, PB5, PB6, PB9 to PB15, PC7 and PC9 to PC11 | The silent-signal pin must not be one of these, or the bootloader could enable the transmitter |

## Bill of materials

Exported from the schematic with `kicad-cli sch export bom`: 63 parts on 30 lines. Capacitors are X7R, 16 V, unless marked.
A blank maker means any maker's part of that value and size.

| Parts | Count | Value | KiCad footprint | Maker and part number |
|---|---|---|---|---|
| C1, C2, C3, C11 | 4 | 1uF 16V | `C_0603_1608Metric` |  |
| C4, C5, C6, C7, C9, C10, C15, C16, C17, C18, C19 | 11 | 100nF | `C_0603_1608Metric` |  |
| C8, C12 | 2 | 4.7uF 16V | `C_0805_2012Metric` |  |
| C13, C14 | 2 | 10pF C0G | `C_0603_1608Metric` |  |
| D1 | 1 | Green, PWR | `LED_0603_1608Metric` |  |
| D2 | 1 | Yellow, BUS | `LED_0603_1608Metric` |  |
| D3 | 1 | Red, TX | `LED_0603_1608Metric` |  |
| D4 | 1 | NUP2105L | `SOT-23` | onsemi NUP2105LT1G |
| F1 | 1 | 1A fast | `Fuse_1206_3216Metric` | Bourns SF-1206F100-2 |
| J1 | 1 | USB4105-GF-A | `USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal` | GCT USB4105-GF-A |
| J2 | 1 | Pin header | `PinHeader_1x06_P2.54mm_Vertical` |  |
| J3, JP1, JP2 | 3 | Pin header | `PinHeader_1x03_P2.54mm_Vertical` |  |
| J4 | 1 | CANH CANL GND | `TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal` | Phoenix Contact MKDS 1,5/3-5,08 |
| Q1 | 1 | BSS84 | `SOT-23` | onsemi BSS84 |
| R1, R2 | 2 | 5.1k | `R_0603_1608Metric` |  |
| R3, R6, R8 | 3 | 1k | `R_0603_1608Metric` |  |
| R4, R5 | 2 | 10k | `R_0603_1608Metric` |  |
| R7 | 1 | 4.7k | `R_0603_1608Metric` |  |
| R9, R10 | 2 | 120R | `R_0805_2012Metric` |  |
| R11 | 1 | 100k | `R_0603_1608Metric` |  |
| SJ1 | 1 | open | `SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm` |  |
| SW1, SW2 | 2 | Push button | `SW_SPST_PTS645Sx43SMTR92` |  |
| SW3 | 1 | LISTEN/OBD | `SW_SPDT_CK_JS102011SAQN` | C&K JS102011SAQN |
| TP1, TP2, TP3, TP4, TP5, TP6, TP7, TP8, TP9, TP10 | 10 | Test point | `TestPoint_Pad_D1.5mm` |  |
| TP11 | 1 | Test point | `TestPoint_Loop_D2.50mm_Drill1.0mm` |  |
| U1 | 1 | USBLC6-2SC6 | `SOT-23-6` | STMicroelectronics USBLC6-2SC6 |
| U2 | 1 | AP2112K-3.3 | `SOT-23-5` | Diodes Incorporated AP2112K-3.3TRG1 |
| U3 | 1 | STM32F446RET6 | `LQFP-64_10x10mm_P0.5mm` | STMicroelectronics STM32F446RET6 |
| U4, U5 | 2 | TJA1051T/3 | `SOIC-8_3.9x4.9mm_P1.27mm` | NXP TJA1051T/3 |
| Y1 | 1 | 8MHz 10pF | `Crystal_SMD_5032-4Pin_5.0x3.2mm` | ECS ECS-80-10-30B-TR |

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
- **USB:** the board has no VBUS signal on PA9, so the USB driver must not wait for one. It must declare the current
  the board draws: 340 mA at worst.
- **No user button:** the development board's button goes away, along with its event.

## Before ordering

**The design**
- [ ] ERC and DRC pass, and every exclusion has a written reason ([design checks](../README.md#design-checks))
- [ ] Every part in the table above has a reason and a read datasheet, and is in stock
- [x] Every microcontroller pin in the schematic matches the pin table above
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
