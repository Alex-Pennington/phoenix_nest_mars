# Phoenix Nest MARS

Open-source HF digital communications software for Military Auxiliary Radio System (MARS) operations.

## Project Status

| Component | Status | Description |
|-----------|--------|-------------|
| **CP** | ✅ Building | Communications Processor - core message handling |
| **SMLinux** | ✅ Building | Station Mapper Linux port - NCS operations & mapping |
| **Crypto** | ✅ Working | KIK/TEK key tape management & MS-DMT encryption |
| **Propagation** | ✅ Working | VOACAP-based coverage prediction |
| **Modem** | 🚧 Future | MIL-STD-188-110A/MELP-e (separate project) |

## Components

### Communications Processor (CP)
Qt5-based message processor for MARS digital traffic. Handles:
- Message formatting and routing
- V3PROTOCOL XML communication with Station Mapper
- MS-DMT binary protocol for modem interface

**Build:**
```bash
cd src/cp
qmake CP-standalone.pro
make
```

### Station Mapper Linux (SMLinux)
Network Control Station (NCS) operations tool:
- Station mapping and visualization
- Roster management
- Terminal interface (V3PROTOCOL)
- VOACAP propagation integration (in progress)

**Build:**
```bash
cd src/smlinux
qmake StationMapper-linux.pro
make
```

**Note:** SMLinux v0.71 is a feature-reduced port. Full VOACAP/heatmap integration pending.

### Crypto
MARS key tape management and MS-DMT encryption infrastructure:

- **Intel HEX parser** with checksum verification for key files
- **KeyTape class** for managing KIK/TEK key collections
- **CipherEngine** for session management and encryption
- **Time-based TEK slot calculation** for key rotation
- **Auto-discovery** of keys from directory structure

Key structure supports ZYG (general membership, 6-month rotation) and ZYI1/ZYI2 (interop, 12-month) key types with hierarchical derivation from shared 32-byte master seeds.

```python
from crypto.cipher_keys import KeyTape, CipherEngine

tape = KeyTape.from_directory("/path/to/keys")
engine = CipherEngine(tape)
ciphertext = engine.encrypt(plaintext)
```

### Propagation
Offline HF propagation prediction using VOACAP/voacapl:
- Area coverage heatmaps
- Channel comparison
- MARS channel database

Requires voacapl installation for real predictions (falls back to simulated data).

```python
from propagation import PropagationPredictor

pred = PropagationPredictor("AAR6XX", 38.47, -82.63)
result = pred.predict_coverage(channel="A4", utc_hour=18)
result.render_heatmap("coverage.png")
```

## Requirements

### Build Dependencies
```bash
# Ubuntu/Debian
sudo apt install build-essential qt5-default qtbase5-dev \
    qtcharts5-dev libqt5serialport5-dev libssl-dev
```

### Python Dependencies (Propagation)
```bash
pip install numpy scipy matplotlib
```

### Optional: voacapl (Real Propagation Predictions)
```bash
sudo apt install gfortran
# Download from https://github.com/jawatson/voacapl
./configure && make && sudo make install
makeitshfbc
```

## Directory Structure

```
phoenix-nest-mars/
├── src/
│   ├── cp/              # Communications Processor
│   ├── smlinux/         # Station Mapper Linux
│   ├── crypto/          # KIK/TEK key management
│   └── propagation/     # VOACAP prediction module
├── docs/                # Documentation
├── scripts/             # Build/utility scripts
└── releases/            # Pre-built binaries
```

## Related Projects

- **MIL-STD-188-110A Modem** - HF modem implementation (separate repo, future)
- **MELP-e Vocoder** - Voice encoding for HF (separate repo, future)

## License

Components have varying licenses:
- CP/SMLinux: Based on GPL sources from MARS community
- Crypto: Project-specific
- Propagation: MIT

## Contributing

This is a Phoenix Nest LLC project for MARS amateur radio operations.

## Acknowledgments

- Original Station Mapper by KYHiTech.com
- voacapl by James Watson (HZ1JW/M0DNS)
- VOACAP by NTIA/ITS
