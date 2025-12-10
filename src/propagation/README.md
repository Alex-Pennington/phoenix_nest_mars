# Phoenix Nest HF Propagation Prediction Module

Offline VOACAP-based HF propagation prediction with heatmap visualization for MARS network operations.

## Features

- **Area Coverage Predictions**: Generate signal reliability heatmaps showing where your station can reach
- **Channel Comparison**: Compare propagation across multiple MARS channels side-by-side
- **Offline Operation**: Uses local voacapl engine - no internet required
- **Simulated Mode**: Falls back to simulated data when voacapl is unavailable (for testing/development)

## Quick Start

```python
from phoenixnest_prop import PropagationPredictor, quick_coverage

# One-liner for quick coverage map
result = quick_coverage("AAR6XX", 38.47, -82.63, "A4", output_path="a4.png")

# Or with more control:
pred = PropagationPredictor(
    callsign="AAR6XX",
    lat=38.47,      # Ashland, KY area
    lon=-82.63
)

# Generate single channel coverage
result = pred.predict_coverage(
    channel="A4",    # MARS channel ID
    utc_hour=18,     # 18Z
    sfi=150          # Solar Flux Index (or use ssn=)
)
result.render_heatmap("a4_coverage.png")

# Compare multiple channels
pred.compare_channels(
    channels=["A4", "A8", "A12"],
    utc_hour=18,
    ssn=100,
    output_path="comparison.png"
)
```

## Installation

### Requirements

- Python 3.8+
- numpy
- scipy  
- matplotlib

### Optional: voacapl for Real Predictions

Without voacapl, the module uses **simulated** propagation data (useful for testing but not accurate predictions).

For real predictions, install voacapl:

```bash
# Ubuntu/Debian
sudo apt install build-essential gfortran

# Download and build voacapl
wget https://github.com/jawatson/voacapl/releases/download/v0.7.6/voacapl-0.7.6.tar.gz
tar xzf voacapl-0.7.6.tar.gz
cd voacapl-0.7.6
./configure && make && sudo make install

# Initialize data directories
makeitshfbc
```

Verify installation:
```bash
voacapl ~/itshfbc
```

## Module Structure

```
phoenixnest_prop/
├── __init__.py       # Main module - PropagationPredictor class
├── coords.py         # Coordinate transformations (WGS84 ↔ Mercator)
├── ice_generator.py  # VOACAP input file generation
├── ig1_parser.py     # VOACAP output file parsing
├── heatmap.py        # Visualization/rendering
├── voacap_runner.py  # voacapl execution wrapper
└── examples/         # Generated output files
    ├── test_area.ICE        # Example VOACAP input
    ├── sample_heatmap.png   # Example coverage map
    └── channel_comparison.png
```

## API Reference

### PropagationPredictor

Main class for generating predictions.

```python
pred = PropagationPredictor(
    callsign="AAR6XX",    # Station callsign
    lat=38.47,            # Latitude (positive North)
    lon=-82.63,           # Longitude (positive East)
    antenna=AntennaConfig(
        filename="DIPOLE.ANT",
        bearing=90.0,     # Main beam direction
        power_kw=0.1      # Power in kilowatts
    )
)
```

#### predict_coverage()

Generate area coverage prediction.

```python
result = pred.predict_coverage(
    channel="A4",         # MARS channel ID, OR
    freq_mhz=5.330,       # Frequency in MHz
    utc_hour=18,          # UTC hour (0-23)
    ssn=100,              # Smoothed Sunspot Number, OR
    sfi=150,              # Solar Flux Index
    bounds=AreaBounds.conus(),  # Geographic bounds
    grid_size=100         # Resolution (100x100 grid)
)
```

Returns `PredictionResult` with:
- `grid` - PropagationGrid with lat/lon/reliability points
- `channel`, `freq_mhz`, `utc_hour`, `ssn` - Input parameters
- `simulated` - True if using simulated (not real) data
- `render_heatmap(path)` - Generate visualization
- `best_coverage_percent` - % of grid with >50% reliability

#### compare_channels()

Compare coverage across multiple channels.

```python
fig = pred.compare_channels(
    channels=["A4", "A8", "A12"],
    utc_hour=18,
    ssn=100,
    output_path="comparison.png"
)
```

### MARS Channels

Built-in channel database:

| Channel | Frequency (MHz) |
|---------|-----------------|
| A1 | 2.7135 |
| A2 | 3.3425 |
| A3 | 4.4905 |
| A4 | 5.330 |
| A5 | 6.9975 |
| A6 | 7.605 |
| A7 | 7.810 |
| A8 | 10.145 |
| A9 | 13.927 |
| A10 | 13.975 |
| A11 | 14.440 |
| A12 | 14.870 |
| A13 | 18.715 |
| A14 | 20.991 |

Add more channels by updating `MARS_CHANNELS` in `ice_generator.py`.

### Solar Data

Convert between SFI and SSN:

```python
from coords import sfi_to_ssn, ssn_to_sfi

ssn = sfi_to_ssn(150)    # → 118
sfi = ssn_to_sfi(100)    # → 136.4
```

## VOACAP File Formats

### Input: .ICE file (Area mode)

```
Model    :ICEPAC
Transmit : 38.47N   82.63W   AAR6XX               Short
Area     :   -126.0    -66.0      24.0      50.0
Gridsize :  200   22
Freqs    :  5.330  0.000  ...
...
```

### Output: .ig1 file (Grid data)

```
 37 37   39.3000  -75.0000  7.29   2 E 49.06 ... 49.06
         ^^^^^^^^  ^^^^^^^^^                      ^^^^^
         LAT       LON                            REL %
```

## Integration with Station Mapper

This module is designed to be compatible with Station Mapper's VOACAP integration:

1. Same input file format (.ICE)
2. Same output parsing (.ig1)
3. Same coordinate projections (WGS84 → Web Mercator)

For full Station Mapper Linux port, see `station_mapper_assessment.md`.

## Troubleshooting

### "voacapl not available"

The module is running in simulated mode. To get real predictions:

1. Install voacapl (see Installation above)
2. Run `makeitshfbc` to create ~/itshfbc directory
3. Verify with `voacapl ~/itshfbc`

### "itshfbc directory not found"

Run `makeitshfbc` to initialize the voacapl data directories.

### Predictions seem wrong

1. Check solar conditions (SSN/SFI) - higher = better propagation
2. Check UTC hour - propagation varies dramatically by time
3. Lower frequencies propagate further at night
4. Higher frequencies propagate further during the day

## Credits

- **VOACAP**: NTIA/ITS (public domain)
- **voacapl Linux port**: James Watson (HZ1JW/M0DNS)
- **Station Mapper**: KYHiTech.com (format reference)
- **Phoenix Nest integration**: Claude + Rayven

## License

This propagation module is provided for MARS network operations.
voacapl is public domain software from the US Government.
