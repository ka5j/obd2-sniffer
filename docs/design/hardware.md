# Hardware and wiring

## In short

The project runs on an off-the-shelf STM32 development board. A small add-on chip, the CAN
transceiver, sits between the board and the car, translating the car's network signals into
signals the microcontroller understands. The laptop connects to the board with a normal USB
cable, which both powers the board and carries the data.

Nothing is powered from the car. That keeps the car's electrical system and the laptop separate,
and it means a wiring mistake can't damage either one.

## Parts

| Part | What it does |
|---|---|
| NUCLEO-F446RE | Development board with the STM32F446RE microcontroller and a built-in USB programmer/debugger (ST-LINK) |
| SN65HVD230 module | CAN transceiver: converts between the car's two-wire differential CAN signal and the microcontroller's 3.3 V logic signals |
| OBD2 cable | Connects to the car's diagnostic port. Only three of its 16 pins are used |

### Microcontroller at a glance

| | |
|---|---|
| Core | Arm Cortex-M4 with floating-point unit |
| Speed | 180 MHz |
| Memory | 512 KB flash (program), 128 KB RAM (data) |
| CAN controllers | 2 (one talks to the car; the other can act as a fake engine computer for testing) |

## Connecting to a car

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

## Pin assignments

These are the microcontroller pins the project uses. The LED, button and serial pins are defined
in `firmware/include/board.h`; the CAN pins are assigned for the CAN driver.

| Function | Pin | Board header | Notes |
|---|---|---|---|
| CAN1 receive | PB8 | Arduino D15 | Main CAN controller (sniffer) |
| CAN1 transmit | PB9 | Arduino D14 | |
| CAN2 receive | PB12 | Morpho CN10 | Second controller (test ECU) |
| CAN2 transmit | PB13 | Morpho CN10 | |
| Serial to laptop (TX) | PA2 | Internal | Routed to the ST-LINK USB serial port |
| Serial to laptop (RX) | PA3 | Internal | |
| Status LED (LD2) | PA5 | Arduino D13 | Green LED |
| User button (B1) | PC13 | On board | Blue button, reads low when pressed |

## Engineering detail

### Clock configuration

The microcontroller's speed comes from its clock system. On the Nucleo board, the ST-LINK
programmer supplies an 8 MHz clock signal. The chip's PLL (a frequency multiplier) turns that into
the 180 MHz the processor runs at:

```
8 MHz input  -> divide by 4 -> 2 MHz -> multiply by 180 -> 360 MHz -> divide by 2 -> 180 MHz
```

Slower copies of that clock feed the peripherals. The CAN controllers and the serial port run from
a 45 MHz bus clock. If the 8 MHz input is missing, the firmware falls back to the chip's internal
16 MHz oscillator and reports it at start-up. The internal oscillator is less precise, which can
cause CAN errors, so the report helps explain them. Full detail is in
[build-and-boot.md](build-and-boot.md#clock-tree).

### CAN bit timing

Cars use 500 kbit/s on the diagnostic CAN bus (some use 250 kbit/s). The CAN controller divides
its 45 MHz clock to get there. Each bit is split into 18 small time slices; the controller samples
the signal 16 slices in (an 88.9% sample point), late in the bit, after the signal has settled.

| Bit rate | Prescaler | Time quanta per bit | Sample point |
|---|---|---|---|
| 500 kbit/s | 5 | 18 (1 + 15 + 2) | 88.9% |
| 250 kbit/s | 10 | 18 (1 + 15 + 2) | 88.9% |
