# Hardware and wiring

Part of the [project documentation](../README.md).

## In short

The project runs on an off-the-shelf STM32 development board. The laptop connects to it with a
normal USB cable, which both powers the board and carries the data. Today that is all the
hardware there is: the board's CAN controller runs in loopback, receiving its own messages, so
everything can be developed and tested with no car and no extra parts.

To connect to a car, a small add-on chip, the CAN transceiver, will sit between the board and
the car, translating the car's network signals into signals the microcontroller understands.
That connection is designed below but not built yet. Even then, the development board takes
nothing from the car's power supply, which keeps the car's electrical system and the laptop separate.
That reduces what a wiring mistake can do, but doesn't remove it:
[safety.md](safety.md#what-the-layers-dont-cover) lists the risks that remain.

## Parts

| Part | What it does | Status |
|---|---|---|
| NUCLEO-F446RE | Development board with the STM32F446RE microcontroller and a built-in USB programmer/debugger (ST-LINK) | Used today |
| SN65HVD230 module | CAN transceiver: converts between the car's two-wire differential CAN signal and the microcontroller's 3.3 V logic signals | For the car connection, not built yet |
| OBD2 cable | Connects to the car's diagnostic port. Only three of its 16 pins are used | For the car connection, not built yet |

### Microcontroller at a glance

| | |
|---|---|
| Core | Arm Cortex-M4 with floating-point unit |
| Speed | 168 MHz (180 MHz possible; 168 MHz gives USB its exact 48 MHz) |
| Memory | 512 KB flash (program), 128 KB RAM (data) |
| CAN controllers | 2: CAN1 captures (today in loopback); CAN2 is reserved for a second node on a test bus |

## Connecting to a car

This is the design for the car connection; it has not been built or tested yet.

| OBD2 pin | Signal | Connects to |
|---|---|---|
| 6 | CAN High | Transceiver CANH |
| 14 | CAN Low | Transceiver CANL |
| 5 | Signal ground | Board GND |

Rules for the car connection:

- **Power the board from the laptop's USB**, never from the OBD2 port's 12 V pin (pin 16).
- **Remove the transceiver's 120-ohm termination resistor** (many modules have one fitted). The
  car's network already has its own termination at each end, and adding a third changes the
  electrical behaviour of the bus.
- **Run the laptop on battery** while it is connected to the car, to avoid ground loops through
  the laptop's charger.
- **Check every cable for continuity before it goes near the car**, pin by pin, and check that
  pin 16 (battery) connects to nothing. A wrong pinout could put 12 V on the CAN wires.
- **Check that the car uses classic CAN** at its diagnostic port. This design can't read CAN FD.
- **For listen-only sessions, disconnect the transmit wire** (PB9 to the transceiver), and hold the
  transceiver's transmit input in its idle state, as its datasheet requires. There is no hardware LISTEN
  switch on the development board: see [safety.md](safety.md#before-the-switch-exists).
- **Follow the order of car tests** in [safety.md](safety.md#the-order-of-car-tests): listening
  only, with the engine off, comes first.

The project's own circuit board, which is powered from USB too, is described in
[circuit-board.md](circuit-board.md).

## Pin assignments

These are the microcontroller pins the project uses. The LED, button, serial and CAN1 pins are
defined in `firmware/system/board.h`; the CAN2 pins are reserved for the second controller,
which tests the first over a short desk bus (the self-test).

| Function | Pin | Board header | Notes |
|---|---|---|---|
| CAN1 receive | PB8 | Arduino D15 | Main CAN controller (sniffer) |
| CAN1 transmit | PB9 | Arduino D14 | |
| CAN2 receive | PB12 | Morpho CN10 | Second controller (self-test) |
| CAN2 transmit | PB13 | Morpho CN10 | |
| Serial to laptop (TX) | PA2 | Internal | Routed to the ST-LINK USB serial port |
| Serial to laptop (RX) | PA3 | Internal | |
| Status LED (LD2) | PA5 | Arduino D13 | Green LED |
| User button (B1) | PC13 | On board | Blue button, reads low when pressed |

## Engineering detail

### Clock configuration

The microcontroller's speed comes from its clock system. On the Nucleo board, the ST-LINK
programmer supplies an 8 MHz clock signal. The chip's PLL (a frequency multiplier) turns that into
the 168 MHz the processor runs at, and the exact 48 MHz that USB needs:

```
8 MHz input  -> divide by 4 -> 2 MHz -> multiply by 168 -> 336 MHz -> divide by 2 -> 168 MHz
                                                                    -> divide by 7 ->  48 MHz (USB)
```

Slower copies of that clock feed the peripherals. The CAN controllers and the serial port run from
a 42 MHz bus clock. If the 8 MHz input is missing, the board stops and blinks its LED in the panic
pattern: the chip's internal oscillator isn't precise enough for CAN or USB, so running on it
would only hide the fault. Full detail is in [build-and-boot.md](build-and-boot.md#clock-tree).

### CAN bit timing

Cars use 500 kbit/s on the diagnostic CAN bus (some use 250 kbit/s). The CAN controller divides
its 42 MHz clock to get there. Each bit is split into small time slices (time quanta), and the
controller samples the signal late in the bit, after it has settled along the whole bus.
Automotive networks sample at about 87.5% of the bit (CiA 301, SAE J2284).

The settings are not hard-coded. `lib/can/can_timing.c` searches every exact way to divide the
clock (8 to 25 quanta per bit, within the controller's segment limits) and picks the sample point
closest to 87.5%, preferring more quanta when two are equally close. It is unit-tested, and
`can loopback <kbps>` refuses any rate it can't hit exactly.

| Bit rate | Prescaler | Time quanta per bit | Sample point |
|---|---|---|---|
| 1000 kbit/s | 3 | 14 (1 + 11 + 2) | 85.7% |
| 500 kbit/s | 6 | 14 (1 + 11 + 2) | 85.7% |
| 250 kbit/s | 21 | 8 (1 + 6 + 1) | 87.5% |
| 125 kbit/s | 21 | 16 (1 + 13 + 2) | 87.5% |

At 500 kbit/s, 12 and 21 quanta also divide exactly, but 12 samples at 83.3% or 91.7%, and 21
would need a longer first segment than the controller allows. 14 quanta (85.7%) is the closest to
the target, and within the SAE J2284 range.
