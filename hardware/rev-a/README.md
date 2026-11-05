# Revision A: working brief

Part of the [project documentation](../../docs/README.md). The folder's guide is [here](../README.md), and
[the circuit board](../../docs/design/circuit-board.md) explains why each block exists.

**Status: not started.** The KiCad project goes in this folder.

## In short

Revision A is the first board, built for bring-up and testing rather than to be small. It has room to probe, test points
on every important signal, and a debug header. Its mistakes get fixed in revision B, the finished plug-in device.

This page is a working sheet. Fill it in as the design moves along. Everything on it can change, except the safety rules.

## Blocks and requirements

Every part on the board has to trace to a [requirement](../../docs/design/requirements.md).

| Block | Serves | Done? |
|---|---|---|
| 12 V input from OBD2 pin 16: fuse, reverse-polarity protection, TVS diode, buck converter to 5 V | PWR-1, PWR-2, PWR-3, PWR-4, PWR-6, PWR-7 | |
| Battery-voltage divider into the ADC | PWR-4, PWR-6 | |
| USB-C: power, data and ESD protection | PWR-5, LNK-3, USE-4 | |
| 3.3 V regulator | PWR-5 | |
| Microcontroller: crystal, decoupling, reset, BOOT0, SWD header | USE-4 | |
| CAN1 transceiver, ESD protection, termination solder jumper | CAN-1, CAN-6 | |
| CAN2 transceiver and self-test header | CAN-7 | |
| LISTEN/OBD switch and TX light | SAF-3, USE-3 | |
| PWR and BUS lights | USE-3 | |
| Test points, current-measurement jumper, debug serial header | PWR-6, revision A only | |

## Microcontroller pins

The chip is the STM32F446RE in LQFP64, the same chip as the development board. Check every pin against ST's datasheet
while drawing the schematic: this table is a starting point, not a substitute for it.

| Function | Pin | Where it comes from |
|---|---|---|
| CAN1 receive, transmit | PB8, PB9 | Used by the firmware today ([`board.h`](../../firmware/system/board.h)) |
| Debug serial TX, RX (USART2) | PA2, PA3 | Used by the firmware today ([`board.h`](../../firmware/system/board.h)) |
| CAN2 receive, transmit | PB12, PB13 | Reserved ([pin assignments](../../docs/design/hardware.md#pin-assignments)) |
| USB data D-, D+ | PA11, PA12 | Fixed by the chip's USB peripheral |
| SWD debug SWDIO, SWCLK | PA13, PA14 | Fixed by the chip |
| Crystal in, out | PH0, PH1 | Fixed by the chip |
| BOOT0, reset | BOOT0, NRST | Fixed by the chip |
| Silent-pin control | | To choose |
| Battery-voltage ADC input | | To choose |
| BUS light | | To choose |

## What the firmware will need for this board

Design choices cost firmware work, so here is what's coming. None of it is built yet, and none of it should stop a better
hardware choice.

- **Crystal:** the firmware expects an 8 MHz clock from the development board's programmer. An 8 MHz crystal needs one
  setting changed; any other frequency also needs new clock divider values ([clock tree](../../docs/design/build-and-boot.md#clock-tree)).
- **Pins:** different CAN or serial pins are a small change, in one file.
- **New drivers:** native USB, CAN2, the silent-pin control, the battery-voltage reading and the BUS light.
- **No user button:** the development board's button goes away, along with its event.

## Parts

Part numbers are fixed here as they're chosen. Only choose parts whose datasheet has been read, and check that the board
maker has them in stock.

| Part | Value or number | Why this one | Requirement | Datasheet | In stock? |
|---|---|---|---|---|---|
| | | | | | |

## Before ordering

**The design**
- [ ] ERC and DRC pass, and every exclusion has a written reason ([design checks](../README.md#design-checks))
- [ ] Every part in the table above has a reason, a read datasheet and stock
- [ ] Every microcontroller pin checked against ST's datasheet and the firmware table above
- [ ] Every footprint checked against its datasheet, and the board checked in KiCad's 3D view
- [ ] Reviewed by someone else

**The safety rules** ([safety.md](../../docs/design/safety.md))
- [ ] The transceiver's silent pin is pulled to silent, so a reset, crash or blank chip means silent
- [ ] In LISTEN, the switch stops the firmware enabling the transmitter, however the firmware behaves
- [ ] The TX light is driven from the silent-pin signal itself, not by firmware
- [ ] CAN1's transceiver has a dominant timeout, and tolerates bus faults
- [ ] Termination is open by default
- [ ] CAN2 has no path to the OBD2 pins
- [ ] The fuse comes first on the 12 V input
- [ ] The 12 V rail can't reach the CAN signals through any jumper or header

**The rule for ordering**
- [ ] [The conditions for ordering boards](../README.md#before-ordering-any-boards) are met

## Errata

Mistakes found on the built board go in `ERRATA.md` in this folder, so revision B can fix them.
