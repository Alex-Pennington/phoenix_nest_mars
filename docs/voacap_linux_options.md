# Offline HF Propagation Prediction for Linux

## The Goal
Replicate Station Mapper's killer feature: **VOACAP-driven signal strength heatmaps** projected onto a map showing "where can my station reach on channel X right now?"

This requires:
1. **Propagation engine** - VOACAP/ICEPAC calculations
2. **Area mode support** - Generate grid of predictions, not just point-to-point
3. **Coordinate transformation** - WGS84 → Web Mercator for map overlay
4. **Visualization** - Render heatmap on geographic map

---

## Option 1: voacapl + pythonprop (Recommended)

**What it is:** Native Linux port of VOACAP with Python GUI

**Status:** In Debian experimental (April 2024), should hit stable/Ubuntu soon

**Components:**
- `voacapl` - Command-line Fortran engine (same I/O format as Windows VOACAP)
- `voacapl-data` - Coefficient files, antenna patterns, etc.
- `pythonprop` - Python 3 GTK GUI with:
  - `voacapgui` - Main application
  - `voaP2PPlot` - Point-to-point plotting
  - `voaAreaPlot` - **Area coverage plotting** ← This is what you want!

**Installation (from source):**
```bash
# Dependencies
sudo apt install build-essential gfortran python3-gi python3-cairo \
  python3-matplotlib python3-numpy python3-basemap gir1.2-gtk-3.0

# voacapl
wget https://github.com/jawatson/voacapl/releases/download/v0.7.6/voacapl-0.7.6.tar.gz
tar xzf voacapl-0.7.6.tar.gz
cd voacapl-0.7.6
./configure && make && sudo make install
makeitshfbc  # Creates ~/itshfbc directory structure

# pythonprop
wget https://github.com/jawatson/pythonprop/releases/download/v0.30/pythonprop-0.30.1.tar.gz
tar xzf pythonprop-0.30.1.tar.gz
cd pythonprop-0.30.1
./autogen.sh && ./configure && make && sudo make install
```

**Pros:**
- Same engine as Windows VOACAP (proven, trusted)
- Already has area plotting GUI
- Active development, heading into Debian
- Clean Python codebase

**Cons:**
- Requires building from source (for now)
- GUI is GTK-based (not Qt like Station Mapper)
- Would need integration work for Station Mapper-style overlay

---

## Option 2: Port Station Mapper V0.4 VOACAP Code to Linux

**What it is:** Make Station Mapper's existing VOACAP integration work on Linux

**Changes needed in V0.4:**
```cpp
// window.cpp line 1411, 1493, etc.
// Change from:
process->setProgram(tmpDir+"/bin_win/Icepacw.exe");
// To:
process->setProgram("voacapl");
process->setArguments({QDir::homePath() + "/itshfbc", ...});
```

**Also needs:**
- PROJ library (already have shim from previous session)
- voacapl installed
- Path fixes throughout codebase

**File format compatibility:** Station Mapper generates standard VOACAP .dat/.ICE files. The output .ig1 format is identical between Windows and Linux voacapl.

**Pros:**
- Keep familiar Station Mapper UI
- Already have most of the code
- NCS features intact

**Cons:**
- C++ codebase issues (globals, hardcoded paths, etc.)
- More porting work
- Maintaining forked codebase

---

## Option 3: dvoacap-python (Pure Python VOACAP)

**What it is:** Complete Python reimplementation of DVOACAP engine

**Repository:** https://github.com/skyelaird/dvoacap-python

**Status:** v1.0.1 released Nov 2024, 86.6% validation against reference VOACAP

**Features:**
- Pure Python (numpy/scipy) - no Fortran compilation needed
- Ionospheric modeling
- MUF/reliability calculations
- 2.3x performance improvement over initial release

**Pros:**
- No binary dependencies
- Easy to integrate with phoenixnestmodem
- Modern Python codebase
- Portable everywhere Python runs

**Cons:**
- Newer project, less battle-tested
- May have edge cases where predictions differ from reference
- Need to verify AREA mode support

---

## Option 4: Build Custom Python Propagation Tool

**What it is:** Use voacapl as backend, build custom Python frontend

**Architecture:**
```
┌─────────────────────────────────────────────────────────────┐
│                    phoenixnest-prop                          │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐ │
│  │ Input        │   │ voacapl      │   │ Output Parser    │ │
│  │ Generator    │──▶│ (subprocess) │──▶│ (.ig1 → grid)    │ │
│  │ (.ICE files) │   │              │   │                  │ │
│  └──────────────┘   └──────────────┘   └──────────────────┘ │
│         │                                       │           │
│         ▼                                       ▼           │
│  ┌──────────────┐                     ┌──────────────────┐  │
│  │ Station DB   │                     │ Map Renderer     │  │
│  │ (MARS roster)│                     │ (matplotlib/     │  │
│  │              │                     │  folium/leaflet) │  │
│  └──────────────┘                     └──────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

**Key Python components:**
```python
# ice_generator.py - Create ICEPAC area input files
def generate_area_input(tx_lat, tx_lon, freq_mhz, ssn, utc_hour, 
                        bounds, grid_size=200):
    """Generate .ICE file for area prediction"""
    
# voacap_runner.py - Execute voacapl
def run_area_prediction(ice_file, itshfbc_path="~/itshfbc"):
    """Run voacapl AREA CALC and return .ig1 path"""

# ig1_parser.py - Parse output grid
def parse_ig1_file(ig1_path):
    """Parse .ig1 into numpy array of (lat, lon, reliability)"""

# heatmap.py - Render on map
def render_propagation_heatmap(grid_data, map_bounds, 
                                output_path=None):
    """Render reliability grid as heatmap overlay"""
```

**Pros:**
- Full control over features
- Integrates cleanly with phoenixnestmodem
- Can add MARS-specific features (channel database, roster integration)
- Modern async architecture possible

**Cons:**
- More development work
- Reinventing some wheels

---

## Recommendation for Phoenix Nest

**Phase 1: Get voacapl working**
```bash
# Build and install voacapl
# Verify with: voacapl ~/itshfbc
```

**Phase 2: Prototype Python area prediction**
- Port Station Mapper's .ICE file generation (it's just text templating)
- Parse .ig1 output format
- Render with matplotlib or similar

**Phase 3: Integrate with phoenixnestmodem**
- Add propagation module to existing Python stack
- Build web/Qt UI as needed

**Phase 4 (optional): Full Station Mapper port**
- Once you have working propagation, decide if full SM port is worth it
- Could contribute Linux support back to author

---

## VOACAP File Formats Reference

### Input: .ICE file (Area mode)
```
Model    :ICEPAC
Colors   :Black    :Blue     :Ignore   :Ignore   :Red      :Black with shading
Cities   :RECEIVE.NUL
Nparms   :    1
Parameter:REL      0
Transmit : 38.47N   82.63W   MYCALL               Short
Pcenter  : 38.47N   82.63W   MYCALL
Area     :   -126.0    -66.0      24.0      50.0
Gridsize :  200   22
Method   :   30
Coeffs   :CCIR
Months   :  12.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00
Ssns     :    100      0      0      0      0      0      0      0      0
Qindexs  :   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00
Hours    :     18      0      0      0      0      0      0      0      0
Freqs    :  5.330  0.000  0.000  0.000  0.000  0.000  0.000  0.000  0.000
System   :  125     3.000   90   40     3.000     0.100
Fprob    : 1.00 1.00 1.00 0.00
Rec Ants :[samples \SAMPLE.23   ]  gain=   0.0   0.0
Tx Ants  :[samples \DIPOLE.ANT  ]  0.000  90.0     0.1000
```

### Output: .ig1 file (Grid data)
```
[header lines]
 37 37   39.3000  -75.0000  7.29   2 E 49.06  3.50 200.6 0.861 155.6   0.3-125.9-148.5  22.5  35.6 0.013 0.001 0.081  4.09 -1.31   4.4 10.05  5.92 15.15  8.09 -95.9 0.000 49.06
         ^^^^^^^^  ^^^^^^^^^                                                                                                                                              ^^^^^
         LAT       LON                                                                                                                                                    REL %
```

Position 8-16: Latitude
Position 17-26: Longitude  
Position 99-104: REL (Reliability %)

---

## Solar Data for Offline Use

Station Mapper fetches SFI from NOAA. For offline:

1. **Manual entry** - User inputs current SFI
2. **Predicted SSN tables** - NOAA publishes 12-month forecasts
3. **Local file** - Cache recent solar data

SFI → SSN conversion (from Station Mapper):
```cpp
int SFItoSSN(int SFI) {
    // Approximation formula
    return (int)((SFI - 63.7) / 0.727);
}
```
