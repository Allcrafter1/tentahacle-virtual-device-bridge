# Licensing and distribution boundaries

This document records the release rules for the project. It is not legal
advice.

## What the repository may distribute

- original project source code,
- Home Assistant custom-integration source,
- Freetz-NG package definitions and patches,
- build and validation scripts,
- compiled binaries made solely from this project's source when their
  corresponding source and notices are shipped,
- protocol documentation based on independent interoperability research.

## What the repository must not distribute

- original AVM firmware images,
- firmware images modified with Freetz-NG or this project,
- AVM proprietary executables or libraries,
- configuration backups, support data, credentials, serial numbers or other
  material copied from a user's device.

Freetz-NG explicitly warns that original and modified firmware images must not
be redistributed for licensing reasons. The documented installation therefore
builds the image locally from an original firmware obtained by the user.

## Project license

Original project code is dual-licensed under `MIT OR Apache-2.0`; every user may
choose either license. The MIT option is compatible with inclusion in the
GPL-2.0-only Freetz-NG build, while the Apache-2.0 option provides its explicit
patent grant for uses where that license is compatible. The resulting local
firmware contains components under several licenses; this repository claims no
license over AVM software or third-party projects.

Eclipse Mosquitto is dynamically linked as a system dependency and retains its
EPL-2.0 OR EDL-1.0/BSD-3-Clause license. No Mosquitto source or binary needs to
be copied into this repository.

## Names and support

FRITZ! product names are used descriptively for compatibility. TentAHAcle is
independent community software and is not affiliated with, endorsed by,
sponsored by or supported by FRITZ! GmbH. Users must use this project's issue
tracker rather than the manufacturer's support for problems caused by the
modification. See the full [trademark notice](../TRADEMARKS.md).
