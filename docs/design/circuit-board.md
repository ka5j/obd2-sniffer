# The circuit board

> **Status:** designed, not built yet. This document records the design and the reasons
> behind each choice. The parts named are the kind of part chosen; exact part numbers are fixed
> when the schematic is drawn.

## In short

The project's own circuit board turns the development-board setup into a device that plugs
into a car's diagnostic port. It does the same job as today's setup. It also runs from the car's
own 12 V supply, survives what a car's electrical system can do, and enforces the
[read-only safety design](safety.md) in hardware.

Every part on the board has to trace to a requirement in [requirements.md](requirements.md).
There is nothing on it "just in case": no wireless, no memory card, no display. Each of those
would be one more thing to fail or to make unsafe.

It is built in two revisions:
- **Revision A** is for bring-up and testing. It has room to probe, test points on every
  important signal, and a debug header.
- **Revision B** is the finished, compact device that plugs straight into the port. It takes the
  fixes found on revision A.

| Term | Meaning |
|---|---|
| Schematic | The circuit diagram: which parts there are and how they connect |
| Layout | Where each part and copper track goes on the physical board |
| TVS diode | A part that clamps voltage spikes before they reach the rest of the board |
| Buck converter | An efficient circuit that steps a higher voltage down (here 12 V to 5 V) |
| LDO | A simple regulator that makes a clean, slightly lower voltage (here 5 V to 3.3 V) |
| ESD | Electrostatic discharge: the spark when you touch something; it can destroy chips |
| Bring-up | Powering and testing a new board for the first time, one section at a time |

## Block diagram

```
 OBD2 port                                                              laptop
 pin 16 (12 V) --> fuse --> reverse-polarity --> TVS --> buck 12V->5V --+
                            protection                                  +--> 5 V --> LDO --> 3.3 V
                                                         USB-C VBUS ----+       |            |
                                                                                |            |
                                                      +---------------------+   |   +--------+-------+
 pin 6 (CANH)  ---- ESD ---- CAN1 transceiver -------| STM32F446RE         |<--+   | crystal, reset, |
 pin 14 (CANL) ----/          silent pin <--+        |                     |       | BOOT0, SWD      |
                                            |        | USB (PA11/PA12) ----+--- ESD --- USB-C ---------+--> laptop
               LISTEN/OBD switch -----------+        |                     |
               TX light (on the silent pin)          | ADC <-- battery voltage divider
                                                     |                     |
 self-test header --- CAN2 transceiver --------------|                     |
                                                     +---------------------+
 pins 4, 5 (ground) --- board ground                   PWR, BUS lights
```

## Each block and why it is there

### Power input from the car

The OBD2 port's pin 16 is connected straight to the car's battery, and it stays live with the
car off. A car's supply is harsh: it dips during engine cranking, spikes when a heavy load
switches off (a load dump), and can briefly be 24 V during a jump start.

| Part | Why |
|---|---|
| Fuse | A fault on the board blows the board's fuse, not the car's (PWR-7) |
| Reverse-polarity protection | A wrongly wired cable can't damage the board (PWR-2) |
| TVS diode | Clamps load-dump and other spikes (PWR-3) |
| Buck converter, 40 V-rated input, low standby current | Steps 6 to 16 V down to 5 V efficiently (PWR-1), and draws little while the car is off (PWR-6) |
| Battery-voltage divider into the ADC | Tells the firmware whether the engine is running, whether a crank is in progress, and when to sleep |

### USB power and data

USB-C provides power on the desk and carries the data to the laptop. Its 5 V joins the 12 V
converter's output through a diode, so either source powers the board alone (PWR-5). The
microcontroller's own USB port connects directly to the connector, through an ESD protection
chip. It appears on the laptop as a serial port that needs no driver (LNK-3).

The data pair is routed as a matched pair at 90 ohms. The chip's built-in USB bootloader, entered
with the BOOT0 button, lets the firmware be updated with no debugger (USE-4).

### Microcontroller

The STM32F446RE, the same chip as on the development board, so the firmware carries over.
Around it:
- an 8 MHz crystal (USB needs a precise clock, and the development board borrowed its clock from
  its programmer chip);
- decoupling capacitors at every supply pin, following ST's application note AN4488;
- a reset button;
- a BOOT0 button;
- a SWD debug header for programming and debugging.

### CAN

| Part | Why |
|---|---|
| CAN1 transceiver: automotive grade, 5 V supply with 3.3 V logic, a silent pin, a dominant timeout | The connection to the car. The silent pin is the hardware safety layer. The dominant timeout releases the bus if the transmit input is ever stuck |
| ESD protection on CANH and CANL | Plugging into a car can bring a static discharge with it |
| Termination solder jumper, **open by default** | The car's bus is already terminated at each end. A third resistor would change it (CAN-6). Closed only for desk tests |
| CAN2 transceiver, wired **only** to the self-test header | A second node, so the board can test both transceivers and the wiring before going near a car (CAN-7). It has no path to the OBD2 pins |

### The LISTEN/OBD switch and the TX light

The transceiver's silent pin is pulled to "silent" by a resistor. The firmware can enable the
transmitter only through the switch: in LISTEN, the switch breaks that path, so the pin stays
silent whatever the firmware does. After a reset, a crash, a brown-out, or with a blank chip, the
firmware isn't driving the path either, so the pin is silent then too.

The **TX light** is driven from the silent pin itself, so it shows what the transceiver is
actually doing. If it is off, the board can't transmit.

### Lights

Three single-colour lights, each labelled on the board. With labels, each one's meaning is
obvious, where a colour code would have to be memorised.

| Light | Driven by | Meaning |
|---|---|---|
| PWR | The 3.3 V supply directly | The board has power, even with no firmware |
| BUS | Firmware | Off: no bus found. Blinking: traffic. Fast blinking: bus error, or an unsupported bus |
| TX | The transceiver's silent pin | Lit only while the board is able to transmit |

### Revision A only

- Test points on every supply rail and every bus signal.
- A jumper in the 3.3 V supply, to measure the current the board draws, including in sleep.
- A header for the debug serial port.
- Screw terminals for the OBD2 cable, instead of a plug, so wiring can be checked.

## Left out on purpose

| Not on the board | Why |
|---|---|
| Bluetooth, Wi-Fi | A radio adds a new way in, certification and a lot of firmware; USB does the job |
| SD card, display, buzzer, extra buttons | Not needed for the job; each is something more to fail |
| K-line, J1850 | Older OBD2 protocols; every car sold in North America since 2008 uses CAN |
| CAN FD | The microcontroller's CAN controller is classic CAN; on a CAN FD bus the board stays silent ([safety.md](safety.md#classic-can-only)) |
| Any power output | Nothing on the board feeds power back to the car or to other devices |
| Spare connectors (revision B) | Unused pins are an invitation to wiring mistakes |

## Making it

- **Design:** KiCad. The design files go in `hardware/`.
- **Board:** 4 layers (signal, ground, power, signal). A solid ground layer keeps the USB and CAN
  signals clean, and gives the buck converter's switching currents a short return path.
- **Assembly:** small parts soldered by the board maker; connectors fitted by hand.
- **Bring-up:** one block at a time, each checked before the next:
  1. check the board for shorts;
  2. power from a current-limited supply;
  3. program it over SWD;
  4. check USB;
  5. run the self-test;
  6. test the 12 V input;
  7. only then, the car ([safety.md](safety.md#the-order-of-car-tests)).
