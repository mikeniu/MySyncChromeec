# Zephyr EC USBC Configuration

[TOC]

## Overview

[USB-C] is intended to be a flexible connector supporting multiple data rates,
protocols, and power in either direction. For one connector to support varying
states of power delivery, the system and what it is connected to
must decide who will act as the source (drives power) and sink (consumes power).
Additionally, they need to decide the correct voltage and current for the source
to drive by taking into account not only the source's and sink's capabilities,
but also what the cable can support. Resistance of pull-up and pull-down resistors
on the configuration channel (CC) ports of the USB-C connector are used to
negotiate who is source and who is sink when a new USB-C connection is established.
This allows for setting power characteristics to default USB2 (500mA) default
USB3 (900mA) 1.5A and 3.0A at 5V. Additional power requirements using USB-PD must
then be negotiated by the source and sink over the CC pins of the USB-C connectors.
Beyond power contract negotiations, USB PD messages can be used to enable alternate
modes (Example: DisplayPort) and send a class of messages called Structured Vendor
Defined Message (SVDMs), which are not related to power delivery. The additional
flexiblity and functionality in [USB-C] requires support from the OS.

From the system, USB PD requires a complex state machine as USB PD can
operate in many different modes. This includes but isn't limited to:

*   Negotiated power contracts. Either side of the cable can source or sink
    power up to 100W (if supported by device).
*   Reversed cable mode. This requires a mux to switch the signals before
    getting to the SoC (or AP).
*   Debug accessory mode, e.g. [Case Closed Debugging (CCD)]
*   Multiple uses for the 4 differential pair signals including
    *   USB SuperSpeed mode (up to 4 lanes for USB data)
    *   DisplayPort Alternate Mode (up to 4 lanes for DisplayPort data)
    *   Dock Mode (2 lanes for USB data, and 2 lanes for DisplayPort)
    *   Audio Accessory mode. (1 lane is used for L and R analog audio signal)

For a more complete list of USB-C Power Delivery features, see the
[USB-C PD spec][USB PD Spec Id].

The image below shows a block diagram of a typical [USB-C] setup.

![USBC Block Diagram]

[AP]
[EC]
[PPC]
[TCPC]
[USB-C Mux]
[RETIMER]

## Kconfig Options

[Kconfig.usbc]

Kconfig sub-option

TBD

## Devicetree Nodes

TBD

## Board Specific Code

TBD

## Threads

TBD

## Testing and Debugging

TBD

## Example

TBD

[USB-C]:../ec_terms.md#usb-c
[AP]:../ec_terms.md#ap
[EC]:../ec_terms.md#ec
[PPC]:../ec_terms.md#ppc
[TCPC]:../ec_terms.md#tcpc
[USB-C Mux]:../ec_terms.md#ssmux
[RETIMER]:../ec_terms.md#retimer
[USBC Block Diagram]:../images/usbc_block_diagram.png
[USB PD Spec Id]: https://www.usb.org/document-library/usb-power-delivery
[Kconfig.usbc]:
https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.usbc
