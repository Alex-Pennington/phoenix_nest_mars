"""
Coordinate transformation utilities for propagation heatmaps.

Handles WGS84 (lat/lon) ↔ Web Mercator (EPSG:3857) transformations
without requiring pyproj library.
"""

import math
from typing import Tuple, List
from dataclasses import dataclass

# Earth radius in meters (WGS84 semi-major axis)
EARTH_RADIUS = 6378137.0

# Maximum latitude for Web Mercator (beyond this, projection breaks down)
MAX_LAT = 85.051128779806604


@dataclass
class Point:
    """A geographic point with lat/lon coordinates."""
    lat: float  # Latitude in degrees (-90 to 90)
    lon: float  # Longitude in degrees (-180 to 180)
    
    def to_mercator(self) -> 'MercatorPoint':
        """Convert to Web Mercator coordinates."""
        x, y = wgs84_to_mercator(self.lat, self.lon)
        return MercatorPoint(x, y)


@dataclass  
class MercatorPoint:
    """A point in Web Mercator (EPSG:3857) coordinates."""
    x: float  # Easting in meters
    y: float  # Northing in meters
    
    def to_wgs84(self) -> Point:
        """Convert to WGS84 lat/lon."""
        lat, lon = mercator_to_wgs84(self.x, self.y)
        return Point(lat, lon)


def wgs84_to_mercator(lat: float, lon: float) -> Tuple[float, float]:
    """
    Convert WGS84 lat/lon to Web Mercator x/y.
    
    Args:
        lat: Latitude in degrees
        lon: Longitude in degrees
        
    Returns:
        Tuple of (x, y) in meters
    """
    # Clamp latitude to valid range
    lat = max(min(lat, MAX_LAT), -MAX_LAT)
    
    # Convert to radians
    lat_rad = math.radians(lat)
    lon_rad = math.radians(lon)
    
    # Web Mercator projection
    x = EARTH_RADIUS * lon_rad
    y = EARTH_RADIUS * math.log(math.tan(math.pi / 4 + lat_rad / 2))
    
    return x, y


def mercator_to_wgs84(x: float, y: float) -> Tuple[float, float]:
    """
    Convert Web Mercator x/y to WGS84 lat/lon.
    
    Args:
        x: Easting in meters
        y: Northing in meters
        
    Returns:
        Tuple of (lat, lon) in degrees
    """
    lon = math.degrees(x / EARTH_RADIUS)
    lat = math.degrees(2 * math.atan(math.exp(y / EARTH_RADIUS)) - math.pi / 2)
    
    return lat, lon


def grid_to_mercator(grid_points: List[Tuple[float, float, float]]) -> List[Tuple[float, float, float]]:
    """
    Convert a grid of (lat, lon, value) points to Mercator coordinates.
    
    Args:
        grid_points: List of (lat, lon, value) tuples
        
    Returns:
        List of (x, y, value) tuples in Mercator coordinates
    """
    result = []
    for lat, lon, value in grid_points:
        x, y = wgs84_to_mercator(lat, lon)
        result.append((x, y, value))
    return result


def bounds_to_mercator(min_lat: float, min_lon: float, 
                       max_lat: float, max_lon: float) -> Tuple[float, float, float, float]:
    """
    Convert WGS84 bounding box to Mercator.
    
    Returns:
        Tuple of (min_x, min_y, max_x, max_y)
    """
    min_x, min_y = wgs84_to_mercator(min_lat, min_lon)
    max_x, max_y = wgs84_to_mercator(max_lat, max_lon)
    return min_x, min_y, max_x, max_y


def sfi_to_ssn(sfi: float) -> int:
    """
    Convert Solar Flux Index to Smoothed Sunspot Number.
    
    This is the approximation used by Station Mapper.
    
    Args:
        sfi: Solar Flux Index (typically 65-300)
        
    Returns:
        Smoothed Sunspot Number
    """
    return int((sfi - 63.7) / 0.727)


def ssn_to_sfi(ssn: int) -> float:
    """
    Convert Smoothed Sunspot Number to Solar Flux Index.
    
    Args:
        ssn: Smoothed Sunspot Number
        
    Returns:
        Solar Flux Index
    """
    return ssn * 0.727 + 63.7


def format_lat(lat: float) -> str:
    """Format latitude for VOACAP input (e.g., '38.47N')"""
    if lat >= 0:
        return f"{abs(lat):.2f}N"
    else:
        return f"{abs(lat):.2f}S"


def format_lon(lon: float) -> str:
    """Format longitude for VOACAP input (e.g., '82.63W')"""
    if lon >= 0:
        return f"{abs(lon):.2f}E"
    else:
        return f"{abs(lon):.2f}W"


if __name__ == "__main__":
    # Test transformations
    print("=== Coordinate Transform Tests ===")
    
    # Ashland, KY area
    test_lat, test_lon = 38.47, -82.63
    print(f"\nOriginal: {test_lat}°, {test_lon}°")
    
    x, y = wgs84_to_mercator(test_lat, test_lon)
    print(f"Mercator: x={x:.2f}, y={y:.2f}")
    
    lat2, lon2 = mercator_to_wgs84(x, y)
    print(f"Back to WGS84: {lat2:.6f}°, {lon2:.6f}°")
    
    print(f"\nFormatted for VOACAP: {format_lat(test_lat)} {format_lon(test_lon)}")
    
    # SFI/SSN conversion
    print("\n=== Solar Index Conversion ===")
    test_sfi = 150
    ssn = sfi_to_ssn(test_sfi)
    print(f"SFI {test_sfi} → SSN {ssn}")
    print(f"SSN {ssn} → SFI {ssn_to_sfi(ssn):.1f}")
