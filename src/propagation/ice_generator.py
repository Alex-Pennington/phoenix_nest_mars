"""
VOACAP/ICEPAC area prediction input file generator.

Generates .ICE files for area coverage predictions using the same
format as Station Mapper / Windows VOACAP.
"""

from dataclasses import dataclass, field
from typing import Optional, Tuple
from pathlib import Path
from datetime import datetime, timezone
import os

from coords import format_lat, format_lon, sfi_to_ssn


@dataclass
class AntennaConfig:
    """Antenna configuration for VOACAP."""
    filename: str = "SAMPLE.23"  # Antenna pattern file
    gain: float = 0.0           # Gain in dBi
    bearing: float = 0.0        # Main beam bearing
    power_kw: float = 0.1       # Transmit power in kW


@dataclass
class StationConfig:
    """Station configuration."""
    callsign: str
    lat: float
    lon: float
    antenna: AntennaConfig = field(default_factory=AntennaConfig)


@dataclass
class AreaBounds:
    """Geographic bounds for area prediction."""
    west: float   # Western longitude (negative for West)
    east: float   # Eastern longitude
    south: float  # Southern latitude
    north: float  # Northern latitude
    
    @classmethod
    def conus(cls) -> 'AreaBounds':
        """Return bounds covering CONUS."""
        return cls(west=-126.0, east=-66.0, south=24.0, north=50.0)
    
    @classmethod
    def region6(cls) -> 'AreaBounds':
        """Return bounds for MARS Region 6 (approximate)."""
        return cls(west=-106.0, east=-75.0, south=25.0, north=42.0)


@dataclass
class PropagationParams:
    """Propagation prediction parameters."""
    freq_mhz: float           # Frequency in MHz
    ssn: int                  # Smoothed Sunspot Number
    utc_hour: int             # UTC hour (0-23)
    month: Optional[int] = None  # Month (1-12), None = current
    year: Optional[int] = None   # Year, None = current
    noise_floor_dbm: int = 125   # Noise floor in dBm
    min_snr_db: int = 40         # Minimum SNR required
    grid_size: int = 200         # Grid resolution (200 = 200x200)
    
    def __post_init__(self):
        now = datetime.now(timezone.utc)
        if self.month is None:
            self.month = now.month
        if self.year is None:
            self.year = now.year


def generate_ice_file(
    station: StationConfig,
    params: PropagationParams,
    bounds: AreaBounds,
    output_path: Optional[Path] = None,
    itshfbc_path: Optional[Path] = None
) -> str:
    """
    Generate an ICEPAC area prediction input file (.ICE).
    
    Args:
        station: Transmitting station configuration
        params: Propagation parameters
        bounds: Geographic area bounds
        output_path: Path to write .ICE file (optional)
        itshfbc_path: Base path to itshfbc directory (default: ~/itshfbc)
        
    Returns:
        Contents of the .ICE file as string
    """
    if itshfbc_path is None:
        itshfbc_path = Path.home() / "itshfbc"
    
    # Format station coordinates
    lat_str = format_lat(station.lat)
    lon_str = format_lon(station.lon)
    
    # Format bounds (need specific column widths)
    def pad(val: float, width: int, decimals: int) -> str:
        return f"{val:{width}.{decimals}f}"
    
    # Build ICE file content
    lines = [
        "Model    :ICEPAC",
        "Colors   :Black    :Blue     :Ignore   :Ignore   :Red      :Black with shading",
        "Cities   :RECEIVE.NUL",
        "Nparms   :    1",
        "Parameter:REL      0",
        f"Transmit : {lat_str:>6}   {lon_str:>7}   {station.callsign:<20} Short",
        f"Pcenter  : {lat_str:>6}   {lon_str:>7}   {station.callsign:<20}",
        f"Area     :   {pad(bounds.west, 6, 1)}    {pad(bounds.east, 6, 1)}     {pad(bounds.south, 4, 1)}     {pad(bounds.north, 4, 1)}",
        f"Gridsize :  {params.grid_size}   22",
        "Method   :   30",
        "Coeffs   :CCIR",
        f"Months   :  {params.month:2}.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00",
        f"Ssns     :    {params.ssn:3}      0      0      0      0      0      0      0      0",
        "Qindexs  :   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00",
        f"Hours    :     {params.utc_hour:2}      0      0      0      0      0      0      0      0",
        f"Freqs    : {pad(params.freq_mhz, 5, 3)}  0.000  0.000  0.000  0.000  0.000  0.000  0.000  0.000",
        f"System   :  {params.noise_floor_dbm:3}     3.000   90   {params.min_snr_db}     3.000     0.100",
        "Fprob    : 1.00 1.00 1.00 0.00",
        f"Rec Ants :[samples \\SAMPLE.23   ]  gain=   0.0   0.0",
        f"Tx Ants  :[samples \\{station.antenna.filename:<12}]  0.000 {station.antenna.bearing:5.1f}     {station.antenna.power_kw:.4f}",
    ]
    
    content = "\r\n".join(lines) + "\r\n"
    
    if output_path:
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(content)
    
    return content


def generate_p2p_file(
    tx_station: StationConfig,
    rx_station: StationConfig,
    params: PropagationParams,
    frequencies: list[float],
    output_path: Optional[Path] = None
) -> str:
    """
    Generate a point-to-point VOACAP input file.
    
    Args:
        tx_station: Transmitting station
        rx_station: Receiving station
        params: Propagation parameters
        frequencies: List of frequencies to evaluate (MHz)
        output_path: Path to write file
        
    Returns:
        Contents of the .DAT file
    """
    # Format coordinates
    tx_lat = format_lat(tx_station.lat)
    tx_lon = format_lon(tx_station.lon)
    rx_lat = format_lat(rx_station.lat)
    rx_lon = format_lon(rx_station.lon)
    
    # Build frequency card (11 frequencies per card)
    freq_str = "".join(f"{f:5.2f}" for f in frequencies[:11])
    
    lines = [
        "LINEMAX      55       number of lines-per-page",
        "COEFFS    CCIR",
        f"TIME         {params.utc_hour:2}   {params.utc_hour:2}    1    1",
        f"MONTH      {params.year}{params.month:2}.00",
        f"SUNSPOT    {params.ssn:3}. 0.00",
        "LABEL     STATION1            STATION2",
        f"CIRCUIT   {tx_lat:>6}   {tx_lon:>7}    {rx_lat:>6}   {rx_lon:>7}  S     0",
        f"SYSTEM       1. {params.noise_floor_dbm:3}. 3.00  90. {params.min_snr_db}.0 3.00 0.10",
        "FPROB      1.00 1.00 1.00 0.00",
        f"ANTENNA       1    1    2   30     0.000[samples\\{tx_station.antenna.filename:<12}]{tx_station.antenna.bearing:5.1f}    {tx_station.antenna.power_kw:.4f}",
        f"ANTENNA       2    2    2   30     0.000[samples\\SAMPLE.23    ]{rx_station.antenna.bearing:5.1f}    0.0000",
        f"FREQUENCY {freq_str}",
        "METHOD       30    0",
        "EXECUTE",
        "QUIT",
    ]
    
    content = "\r\n".join(lines) + "\r\n"
    
    if output_path:
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(content)
    
    return content


# MARS channel database (subset - add more as needed)
MARS_CHANNELS = {
    # Primary MARS channels
    "A1": 2.7135,
    "A2": 3.3425,
    "A3": 4.4905,
    "A4": 5.330,
    "A5": 6.9975,
    "A6": 7.605,
    "A7": 7.810,
    "A8": 10.145,
    "A9": 13.927,
    "A10": 13.975,
    "A11": 14.440,
    "A12": 14.870,
    "A13": 18.715,
    "A14": 20.991,
}


def channel_to_freq(channel_id: str) -> float:
    """Convert MARS channel ID to frequency in MHz."""
    return MARS_CHANNELS.get(channel_id.upper(), 0.0)


if __name__ == "__main__":
    # Example: Generate area prediction for Region 6 NCS
    print("=== VOACAP Area File Generator ===\n")
    
    # Configure station (example: Ashland, KY area)
    station = StationConfig(
        callsign="AAR6XX",
        lat=38.47,
        lon=-82.63,
        antenna=AntennaConfig(
            filename="DIPOLE.ANT",
            bearing=90.0,
            power_kw=0.1
        )
    )
    
    # Configure prediction
    params = PropagationParams(
        freq_mhz=channel_to_freq("A4"),  # 5.330 MHz
        ssn=sfi_to_ssn(150),  # Current solar conditions
        utc_hour=18,
        grid_size=100  # Smaller for testing
    )
    
    # Generate for CONUS
    bounds = AreaBounds.conus()
    
    ice_content = generate_ice_file(station, params, bounds)
    print("Generated .ICE file content:")
    print("-" * 60)
    print(ice_content)
    print("-" * 60)
    
    # Save example
    output_dir = Path(__file__).parent / "examples"
    output_dir.mkdir(exist_ok=True)
    
    ice_path = output_dir / "test_area.ICE"
    generate_ice_file(station, params, bounds, output_path=ice_path)
    print(f"\nSaved to: {ice_path}")
