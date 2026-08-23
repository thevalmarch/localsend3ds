# LocalSend3DS

LocalSend3DS is an **unofficial, early-stage LocalSend client for the Nintendo
3DS family**. The current development milestone is LAN discovery using stable
LocalSend protocol v2.2. File transfer is not implemented yet.

Current code includes:

- a native libctru two-screen application;
- Wi-Fi/SOC initialization and clean shutdown;
- bounded IPv4 UDP multicast discovery on `224.0.0.167:53317`;
- typed announcement parsing and a fixed-capacity device registry;
- a nonblocking HTTP server with `/api/localsend/v2/register` and `/info`;
- cryptographically random per-launch HTTP identity via the 3DS PS service;
- host-side parser and registry tests.

The `/register` server is part of the discovery milestone because current
official LocalSend clients answer UDP announcements over HTTP. HTTPS peers are
shown from their UDP announcement but cannot yet be actively registered with;
this limitation is tracked in [protocol notes](docs/protocol-notes.md).

## Build requirements

Install devkitPro using its official macOS package installer, then install the
`3ds-dev` group:

```sh
sudo dkp-pacman -Syu
sudo dkp-pacman -S 3ds-dev
```

Start a new shell (or source `/opt/devkitpro/3dsvars.sh`) and verify:

```sh
printf '%s\n' "$DEVKITPRO" "$DEVKITARM"
command -v arm-none-eabi-gcc 3dsxtool smdhtool 3dslink
```

Build the `.3dsx`:

```sh
make
```

Run platform-independent tests on macOS:

```sh
make -f Makefile.host test
```

Optional deployment is explicit and never part of the normal build:

```sh
make deploy IP=192.168.1.50
```

## First hardware test

Copy `LocalSend3DS.3dsx` to the SD card and launch it through Homebrew Launcher.
Put the console and a Mac running the official LocalSend client on the same
Wi-Fi network. Press Y in LocalSend3DS to announce again. Record whether each
device appears, plus the on-screen socket/error counters. See
[testing.md](docs/testing.md) for the evidence checklist.

Received software is always treated as data. LocalSend3DS will never install or
launch transferred `.cia`, `.3dsx`, or other executable files automatically.

## Status and scope

This repository does not claim real-device compatibility yet. The initial code
has host-side test coverage, but a `.3dsx` build and official-client discovery
still require the devkitPro toolchain and physical 3DS validation. See the
[compatibility matrix](docs/compatibility.md) for verified versus planned work.

LocalSend3DS is licensed under the MIT License. LocalSend is a separate project;
no endorsement by the LocalSend maintainers or Nintendo is implied.
