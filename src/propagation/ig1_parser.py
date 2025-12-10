"""
Parser for VOACAP/ICEPAC area prediction output files (.ig1).

The .ig1 format contains a grid of propagation predictions with
coordinates and reliability values.
"""

import numpy as np
from dataclasses import dataclass
from typing import List, Tuple, Optional, Generator
from pathlib import Path


@dataclass
class GridPoint:
    """A single point in the propagation grid."""
    lat: float           # Latitude (degrees)
    lon: float           # Longitude (degrees)
    reliability: float   # Circuit reliability (0-100%)
    snr: float = 0.0     # Signal-to-noise ratio (dB)
    muf: float = 0.0     # Maximum usable frequency (MHz)


@dataclass
class PropagationGrid:
    """Complete propagation prediction grid."""
    points: List[GridPoint]
    grid_size: int
    freq_mhz: float
    utc_hour: int
    ssn: int
    tx_lat: float
    tx_lon: float
    
    @property
    def min_lat(self) -> float:
        return min(p.lat for p in self.points)
    
    @property
    def max_lat(self) -> float:
        return max(p.lat for p in self.points)
    
    @property
    def min_lon(self) -> float:
        return min(p.lon for p in self.points)
    
    @property
    def max_lon(self) -> float:
        return max(p.lon for p in self.points)
    
    def to_numpy(self) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        """
        Convert to numpy arrays for plotting.
        
        Returns:
            Tuple of (lats, lons, reliabilities) arrays
        """
        lats = np.array([p.lat for p in self.points])
        lons = np.array([p.lon for p in self.points])
        rels = np.array([p.reliability for p in self.points])
        return lats, lons, rels
    
    def to_grid_array(self) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        """
        Convert to 2D grid arrays for contour/heatmap plotting.
        
        Returns:
            Tuple of (lat_grid, lon_grid, rel_grid) 2D arrays
        """
        # Get unique lat/lon values
        unique_lats = sorted(set(p.lat for p in self.points), reverse=True)
        unique_lons = sorted(set(p.lon for p in self.points))
        
        n_lat = len(unique_lats)
        n_lon = len(unique_lons)
        
        # Create lookup for grid position
        lat_idx = {lat: i for i, lat in enumerate(unique_lats)}
        lon_idx = {lon: i for i, lon in enumerate(unique_lons)}
        
        # Build grid
        rel_grid = np.zeros((n_lat, n_lon))
        for p in self.points:
            i = lat_idx.get(p.lat)
            j = lon_idx.get(p.lon)
            if i is not None and j is not None:
                rel_grid[i, j] = p.reliability
        
        lon_grid, lat_grid = np.meshgrid(unique_lons, unique_lats)
        return lat_grid, lon_grid, rel_grid


def parse_ig1_line(line: str) -> Optional[GridPoint]:
    """
    Parse a single line from .ig1 file.
    
    Format (fixed-width columns):
    " 37 37   39.3000  -75.0000  7.29   2 E 49.06  3.50 200.6 0.861 155.6   0.3-125.9-148.5  22.5  35.6 0.013 0.001 0.081  4.09 -1.31   4.4 10.05  5.92 15.15  8.09 -95.9 0.000 49.06"
             ^^^^^^^^  ^^^^^^^^^                                                                                                                                              ^^^^^
             LAT       LON                                                                                                                                                    REL
    """
    if len(line) < 100:
        return None
    
    try:
        # Extract fields by position
        lat = float(line[8:16].strip())
        lon = float(line[17:26].strip())
        
        # REL is at position 99-104 in the original format
        # But exact position can vary - try to find it
        # The reliability is typically the last or second-to-last value
        # For safety, extract from known position
        rel_str = line[99:105].strip() if len(line) > 104 else "0"
        
        # If that doesn't work, try parsing from end
        if not rel_str or rel_str == "":
            parts = line.split()
            if len(parts) >= 2:
                rel_str = parts[-1]  # Last value is often REL
        
        reliability = float(rel_str)
        
        # Clamp reliability to valid range
        reliability = max(0.0, min(100.0, reliability))
        
        return GridPoint(lat=lat, lon=lon, reliability=reliability)
        
    except (ValueError, IndexError):
        return None


def parse_ig1_file(filepath: Path, grid_size: int = 200) -> PropagationGrid:
    """
    Parse a complete .ig1 file.
    
    Args:
        filepath: Path to .ig1 file
        grid_size: Expected grid size (for validation)
        
    Returns:
        PropagationGrid object
    """
    filepath = Path(filepath)
    points = []
    
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    # Skip header lines (typically 3)
    data_lines = lines[3:]
    
    for line in data_lines:
        point = parse_ig1_line(line)
        if point:
            points.append(point)
    
    # Create grid (metadata would need to be extracted from .ICE file)
    return PropagationGrid(
        points=points,
        grid_size=grid_size,
        freq_mhz=0.0,  # Would need to parse from ICE
        utc_hour=0,
        ssn=0,
        tx_lat=0.0,
        tx_lon=0.0
    )


def generate_sample_grid(
    tx_lat: float = 38.47,
    tx_lon: float = -82.63,
    bounds: Tuple[float, float, float, float] = (-126.0, -66.0, 24.0, 50.0),
    grid_size: int = 50,
    freq_mhz: float = 5.330,
    ssn: int = 100
) -> PropagationGrid:
    """
    Generate a sample/simulated propagation grid for testing.
    
    Uses simplified distance-based attenuation model (NOT real propagation!).
    This is only for testing the visualization pipeline.
    
    Args:
        tx_lat, tx_lon: Transmitter location
        bounds: (west, east, south, north) bounds
        grid_size: Number of points per dimension
        freq_mhz: Frequency in MHz (affects range)
        ssn: Sunspot number (affects reliability)
        
    Returns:
        PropagationGrid with simulated data
    """
    import math
    
    west, east, south, north = bounds
    
    # Generate grid
    lats = np.linspace(north, south, grid_size)
    lons = np.linspace(west, east, grid_size)
    
    points = []
    
    # Simple propagation model:
    # - Reliability decreases with distance
    # - Higher SSN = better long-distance propagation
    # - Optimal skip distance varies with frequency
    
    optimal_skip_km = 1500 + (freq_mhz - 5.0) * 100  # Very rough approximation
    
    for lat in lats:
        for lon in lons:
            # Calculate great circle distance (approximate)
            dlat = math.radians(lat - tx_lat)
            dlon = math.radians(lon - tx_lon)
            a = math.sin(dlat/2)**2 + math.cos(math.radians(tx_lat)) * \
                math.cos(math.radians(lat)) * math.sin(dlon/2)**2
            c = 2 * math.asin(math.sqrt(a))
            distance_km = 6371 * c  # Earth radius in km
            
            # Simulated reliability based on distance
            # Peak around optimal skip distance, falloff beyond
            if distance_km < 500:
                # Ground wave range
                rel = 90 - distance_km * 0.1
            elif distance_km < optimal_skip_km:
                # Skip zone building up
                rel = 30 + (distance_km - 500) / (optimal_skip_km - 500) * 60
            else:
                # Beyond optimal, reliability drops
                excess = distance_km - optimal_skip_km
                rel = 90 - (excess / 1000) * 20
            
            # SSN affects reliability at longer distances
            if distance_km > 1000:
                ssn_factor = ssn / 100.0
                rel *= ssn_factor
            
            # Add some noise
            rel += np.random.normal(0, 5)
            
            # Clamp to valid range
            rel = max(0, min(100, rel))
            
            points.append(GridPoint(lat=lat, lon=lon, reliability=rel))
    
    return PropagationGrid(
        points=points,
        grid_size=grid_size,
        freq_mhz=freq_mhz,
        utc_hour=12,
        ssn=ssn,
        tx_lat=tx_lat,
        tx_lon=tx_lon
    )


if __name__ == "__main__":
    print("=== Propagation Grid Parser Test ===\n")
    
    # Generate sample grid
    grid = generate_sample_grid(
        tx_lat=38.47,
        tx_lon=-82.63,
        grid_size=20,
        freq_mhz=5.330,
        ssn=100
    )
    
    print(f"Generated {len(grid.points)} grid points")
    print(f"Lat range: {grid.min_lat:.2f} to {grid.max_lat:.2f}")
    print(f"Lon range: {grid.min_lon:.2f} to {grid.max_lon:.2f}")
    
    # Show some sample points
    print("\nSample points:")
    for i in [0, len(grid.points)//4, len(grid.points)//2, -1]:
        p = grid.points[i]
        print(f"  ({p.lat:.2f}, {p.lon:.2f}): {p.reliability:.1f}%")
    
    # Convert to numpy
    lats, lons, rels = grid.to_numpy()
    print(f"\nNumpy arrays: {lats.shape}, {lons.shape}, {rels.shape}")
    print(f"Reliability range: {rels.min():.1f}% to {rels.max():.1f}%")
