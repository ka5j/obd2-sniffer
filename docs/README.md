# Documentation

All the project's documents, grouped by what they're for. Each one starts with a short
plain-language summary, followed by the engineering detail.

## Where to start

| You want to... | Read, in order |
|---|---|
| Understand what this is, in a few minutes | The [project page](../README.md), then [how it works](how-it-works.md) |
| Run it yourself | [Getting started](getting-started.md), then [the laptop tool](../host/README.md) |
| Judge the engineering | [How it works](how-it-works.md), [safety](design/safety.md), [requirements](design/requirements.md), [testing](design/testing.md), then [how the firmware fits together](design/architecture.md) |
| Learn how CAN and OBD2 work in code | [How it works](how-it-works.md), [CAN capture](design/can-capture.md), then [OBD2](design/obd2.md) |
| Follow the circuit board | [Circuit board](design/circuit-board.md), [safety](design/safety.md), then [hardware and wiring](design/hardware.md) |

## Start here

- [How it works](how-it-works.md): the whole project explained from the beginning, with no car
  knowledge assumed.
- [Getting started](getting-started.md): from nothing to running, with or without a board.

## Using it

- [The laptop tool](../host/README.md): the desktop app, command line, shell, simulator and Python
  library.
- [Serial console and protocol](protocol.md): the board's commands, and how a program talks to it.
- [Hardware and wiring](design/hardware.md): the parts, pin assignments, and the rules for connecting
  to a car.

## How it's designed

- [How the firmware fits together](design/architecture.md): a one-page map of the parts, the
  interrupts and the main loop.
- [CAN capture](design/can-capture.md): how frames get from the bus to the laptop with none lost
  unnoticed.
- [OBD2](design/obd2.md): asking the car for live data, trouble codes and its VIN.
- [Serial console internals](design/serial-console.md): interrupts, DMA and ring buffers.
- [Watchdog and crash reporting](design/fault-handling.md): recovering from a hang or a crash, and
  saying why.
- [Build system and start-up sequence](design/build-and-boot.md): the build, the memory layout, and
  what happens from power-on to the main loop.
- [Circuit board](design/circuit-board.md): the design of the project's own board, which isn't built
  yet.

## How it's proven

- [Requirements](design/requirements.md): what the device must do, each with its test and its status
  today.
- [Safety](design/safety.md): the read-only design, its layers, and the risks that remain.
- [Testing and code coverage](design/testing.md): how each kind of check works, and the latest
  results from the board.
- [MISRA C coding standard](design/misra.md): the coding rules, and every documented exception.
- [Live quality report](https://ka5j.github.io/obd2-sniffer/): test results, coverage and MISRA
  findings, published by CI.
