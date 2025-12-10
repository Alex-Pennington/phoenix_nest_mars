# Phoenix Nest MARS - AI Coding Instructions

## Project Overview

HF digital communications software for Military Auxiliary Radio System (MARS) operations. Four main components:

| Component | Language | Build | Purpose |
|-----------|----------|-------|---------|
| **CP** | C++/Qt5 | `qmake CP-standalone.pro && make` | Message processor, modem interface |
| **SMLinux** | C++/Qt5 | `qmake StationMapper-linux.pro && make` | NCS station mapping & visualization |
| **Crypto** | Python 3 | N/A | KIK/TEK key tape management |
| **Propagation** | Python 3 | N/A | VOACAP-based HF coverage prediction |

## Build Commands

```bash
# Full build (Linux)
./scripts/build.sh all

# Individual components
cd src/cp && qmake CP-standalone.pro && make
cd src/smlinux && qmake StationMapper-linux.pro && make

# Python modules require: numpy scipy matplotlib
# Optional: voacapl for real propagation (falls back to simulated)
```

## Architecture & Data Flows

### Communication Protocols

- **V3PROTOCOL**: XML-based messaging between CP ↔ Station Mapper. Parsed in `src/smlinux/classxml.cpp` and `src/cp/classxml.cpp`. Root element `<V3PROTOCOL>` wraps `<HEADER>` (metadata) and `<BODY>` (payload).

- **CP ↔ Modem Interface**: Two TCP connections to modem (default ports 5001/5000):

  **Command Port (text, line-based)** - Status messages, newline-delimited:
  - `OK` - Command acknowledged
  - `STATUS:TX:TRANSMIT` / `STATUS:TX:IDLE` - Transmit state
  - `STATUS:RX:NO DCD` - Receive complete, no carrier
  - `STATUS:RX:<status>` - Receiving with status
  - `<speed><S|L>` - Speed/interleave (e.g., `300S`, `1200L`)

  **Data Port (raw bytes)** - No framing, just a pipe:
  - TX: CP writes bytes → modem transmits
  - RX: Modem receives → CP reads bytes
  
  See `src/cp/mainwindow.cpp` `slotReadMSDMTCMDSocket()` for command parsing.

### Key Hierarchy (Crypto)

```
KIK (96 bytes)
├── Seed (0-31):    Shared across key types for same period
├── Variant (32-63): Differentiates ZYG/ZYI1/ZYI2
└── MAC (64-95):    Integrity verification

TEK: 256 bytes (ZYG) or 464 bytes (ZYI1/ZYI2) → 16-byte blocks for time slots
```

Key files use Intel HEX format. See `src/crypto/cipher_keys.py` for `IntelHexParser`, `KeyTape`, `CipherEngine`.

### Propagation Pipeline

```
StationConfig → ICE file → voacapl → .ig1 grid → heatmap PNG
```

MARS channels defined in `src/propagation/ice_generator.py` (`MARS_CHANNELS` dict). Coordinates use WGS84 → Web Mercator projection.

## Code Patterns

### Qt5 Conventions (CP/SMLinux)

- **Globals**: Configuration in `globals.h` struct `Settings globalConfigSettings`
- **Signals/Slots**: Cross-component communication (see `mainwindow.cpp` connect statements)
- **UI**: `.ui` files with corresponding `dialog*.cpp/.h` pairs
- **Class naming**: `Class*` prefix for data/logic, `Dialog*` for UI

### Python Module Patterns (Propagation/Crypto)

```python
# Propagation usage
from phoenixnest_prop import PropagationPredictor
pred = PropagationPredictor("AAR6XX", lat, lon)
result = pred.predict_coverage(channel="A4", utc_hour=18, ssn=100)
result.render_heatmap("output.png")

# Crypto usage  
from crypto.cipher_keys import KeyTape, CipherEngine
tape = KeyTape.from_directory("/path/to/keys")
engine = CipherEngine(tape)
```

## Critical Files

| Path | Purpose |
|------|---------|
| `src/cp/mainwindow.cpp` | CP main logic (6000+ lines) |
| `src/cp/globals.h` | Settings structs, app constants |
| `src/smlinux/mainwindowtab.cpp` | Tab-based NCS UI |
| `src/propagation/__init__.py` | `PropagationPredictor` class |
| `src/crypto/cipher_keys.py` | Key parsing & cipher operations |

## Stubbed/Placeholder Code

The GPL build has stubbed components (see `src/cp/BUILD_README.md`):
- `cipherlibraryclass_stub.cpp` - Passthrough cipher (plaintext only)
- `keyloader.h` - Stub showing "not available"
- Crypto module uses XOR placeholder - real MIL-STD algorithm TBD

## Platform Notes

- **Qt version**: Qt 5.15+ required. Check with `qmake --version`
- **Linux deps**: `qtbase5-dev qtcharts5-dev libqt5serialport5-dev libssl-dev`
- **voacapl**: Run `makeitshfbc` after install to create `~/itshfbc` data dirs
- **SMLinux v0.71**: Feature-reduced port; full VOACAP integration pending

## When Modifying

1. **Adding MARS channels**: Update `MARS_CHANNELS` dict in `src/propagation/ice_generator.py`
2. **V3PROTOCOL changes**: Update both `src/cp/classxml.cpp` and `src/smlinux/classxml.cpp`
3. **Settings changes**: Modify structs in `globals.h`, update serialization in `dialogsettings.cpp`
4. **New dialogs**: Create `.ui` + `dialog*.cpp/.h`, add to `.pro` SOURCES/HEADERS/FORMS
