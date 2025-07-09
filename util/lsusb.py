#! /usr/bin/env python3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import re
import subprocess
from dataclasses import dataclass


def command(cmd):
    r = subprocess.run(f"{cmd} 2>/dev/null", shell=True, stdout=subprocess.PIPE)
    if r.returncode != 0:
        raise Exception(r)
    return r.stdout.decode("ascii", "ignore")


@dataclass
class DeviceData:
    bus: int
    dev: int
    id: str
    description: str
    product_description: str
    config: str
    interfaces: list


@dataclass
class InterfaceData:
    number: str
    name: str
    endpoints: list


def get_device_details(bus, dev, id, description):
    r = command(f"lsusb -v -s {bus}:{dev}")
    interfaces = []
    details = {}
    cur_if = None
    for line in r.splitlines():
        if "idVendor" in line:
            details["vendor"] = (
                re.search(r"idVendor +0x[^ ]+ (.*)", line).group(1).strip()
            )
        if "idProduct" in line:
            details["product1"] = (
                re.search(r"idProduct +0x[^ ]+ (.*)", line).group(1).strip()
            )
        if "iProduct" in line:
            details["product2"] = (
                re.search(r"iProduct +[^ ]+ (.*)", line).group(1).strip()
            )
        if "iSerial" in line:
            details["serial"] = re.search(r"iSerial +[^ ]+ (.*)", line).group(1).strip()
        if "iConfiguration" in line:
            details["config"] = (
                re.search(r"iConfiguration +[^ ]+ (.*)", line).group(1).strip()
            )
        if "bInterfaceNumber" in line:
            i = re.search(r"bInterfaceNumber +([^ ]+)", line).group(1).strip()
            cur_if = InterfaceData(i, None, [])
        if "iInterface" in line:
            i = re.search(r"iInterface +[^ ]+ (.*)", line).group(1).strip()
            cur_if.name = i
        if "bEndpointAddress" in line:
            ep = re.search(r"bEndpointAddress +0x[^ ]+ +EP (.*)", line).group(1).strip()
            cur_if.endpoints.append(ep)
        if line == "    Interface Descriptor:":
            if cur_if:
                interfaces.append(cur_if)
            cur_if = None
    if cur_if:
        interfaces.append(cur_if)
    vendor = details.get("vendor", "").split(" ")[0]
    product = ""
    if details.get("product2", ""):
        product = details["product2"]
    elif details.get("product1", ""):
        product = details["product1"]
    serial = details.get("serial", "")
    product_description = f"{vendor} {product} ({serial})"
    return DeviceData(
        bus,
        dev,
        id,
        description,
        product_description,
        details.get("config", ""),
        interfaces,
    )


def get_devices():
    result = {}
    r = command("lsusb")
    for line in r.splitlines():
        m = re.search(r"Bus (\d+) Device (\d+): ID ([^ ]+) (.*)", line)
        bus, dev, id, description = m.groups()
        bus, dev = int(bus), int(dev)
        result[(bus, dev)] = get_device_details(bus, dev, id, description)
    return result


def get_uarts():
    result = {}
    try:
        r = command("ls -al /dev/serial/by-path/")
    except:
        return {}
    for line in r.splitlines():
        if "/tty" not in line:
            continue
        m = re.search(r" pci-.*-usb[^:]+:([^:]+):(\d\.\d+).* -> ../../(tty.*\d+)", line)
        path, i, tty = m.groups()
        m = re.match(r"1\.(\d+)", i)
        i = m.group(1)
        result.setdefault(path, {})[i] = tty
    return result


@dataclass
class TreeData:
    bus: int
    port: int
    dev: int
    level: int
    parent: "TreeData"
    children: list
    path: list


def print_tree_device(
    td, uarts, devices, show_interfaces, show_endpoints, all_interfaces
):
    details = devices[(td.bus, td.dev)]
    if td.parent is None:
        print(f"Root: {td.bus}-{td.port} dev={td.dev} {details.product_description}")
    else:
        path = ".".join(str(p) for p in td.path)
        indent = " " * (2 * td.level)
        out = f"{indent}{td.bus}-{path} dev={td.dev}"
        print(f"{out:25}{details.product_description} {details.config}")
        if show_interfaces:
            interfaces = details.interfaces
            if not all_interfaces:
                interfaces = [i for i in interfaces if i.name]
            for i in interfaces:
                uart = ""
                if path in uarts:
                    uart = uarts[path].get(i.number, "")
                eps = ""
                if show_endpoints:
                    eps = ", ".join(i.endpoints)
                    eps = f" ({eps})"
                print(f'{" "*30}{i.number} {i.name:16} {uart:8}{eps}')

    for c in sorted(td.children, key=lambda k: (k.bus, k.port, k.dev)):
        print_tree_device(
            c, uarts, devices, show_interfaces, show_endpoints, all_interfaces
        )


def print_tree(show_interfaces, show_endpoints, all_interfaces):
    show_interfaces = show_interfaces or show_endpoints or all_interfaces
    tree = command("lsusb -t")
    roots = []
    parents = {}
    prev_td = None
    for line in tree.splitlines():
        if line.startswith("/:"):
            m = re.search(r"Bus (\d+).Port (\d+): Dev (\d+)", line)
            bus, port, dev = [int(x) for x in m.groups()]
            td = TreeData(bus, port, dev, 0, None, [], [])
            roots.append(td)
            parents[0] = td
            prev_td = td
        else:
            m = re.search(r"( +)\|__ Port (\d+): Dev (\d+)", line)
            level, port, dev = m.groups()
            level = len(level) // 4
            port, dev = int(port), int(dev)
            if (level, port, dev) == (prev_td.level, prev_td.port, prev_td.dev):
                continue
            parent = parents[level - 1]
            path = parent.path.copy()
            path.append(port)
            td = TreeData(parent.bus, port, dev, level, parent, [], path)
            parent.children.append(td)
            parents[level] = td
            prev_td = td
    uarts = get_uarts()
    devices = get_devices()
    parent_ports = {}
    for r in sorted(roots, key=lambda k: (k.bus, k.port, k.dev)):
        print_tree_device(
            r, uarts, devices, show_interfaces, show_endpoints, all_interfaces
        )


def main():
    parser = argparse.ArgumentParser(
        description="Show USB device details: interfaces, UARTs, endpoints."
    )
    parser.add_argument(
        "--interfaces", action="store_true", help="Show named interfaces"
    )
    parser.add_argument("--endpoints", action="store_true", help="Show endpoints")
    parser.add_argument(
        "--allinterfaces", action="store_true", help="Show all interfaces"
    )
    args = parser.parse_args()
    print_tree(args.interfaces, args.endpoints, args.allinterfaces)


if __name__ == "__main__":
    main()
