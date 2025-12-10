"""
Phoenix Nest HF Propagation Prediction Module

Provides offline VOACAP-based propagation prediction with heatmap visualization
for MARS network operations.

Usage:
    from phoenixnest_prop import PropagationPredictor
    
    # Create predictor
    pred = PropagationPredictor(
        callsign="AAR6XX",
        lat=38.47,
        lon=-82.63
    )
    
    # Generate area coverage
    result = pred.predict_coverage(
        channel="A4",   # or freq_mhz=5.330
        utc_hour=18,
        ssn=100         # or sfi=150
    )
    
    # Visualize
    result.render_heatmap("coverage.png")
"""

from dataclasses import dataclass, field
from typing import Optional, List, Tuple, Union
from pathlib import Path
from datetime import datetime, timezone

# Local imports
from coords import sfi_to_ssn, ssn_to_sfi
from ice_generator import (
    StationConfig, AntennaConfig, PropagationParams, 
    AreaBounds, generate_ice_file, MARS_CHANNELS, channel_to_freq
)
from ig1_parser import PropagationGrid, parse_ig1_file, generate_sample_grid
from heatmap import render_heatmap_with_basemap, render_multi_channel_comparison, HeatmapStyle
from voacap_runner import VoacapRunner, VoacapConfig, check_voacapl_installation


@dataclass
class PredictionResult:
    """Result of a propagation prediction."""
    grid: PropagationGrid
    channel: str
    freq_mhz: float
    utc_hour: int
    ssn: int
    tx_callsign: str
    tx_lat: float
    tx_lon: float
    bounds: AreaBounds
    simulated: bool = False  # True if using simulated data (voacapl unavailable)
    
    def render_heatmap(
        self,
        output_path: Optional[Path] = None,
        title: Optional[str] = None,
        style: Optional[HeatmapStyle] = None
    ):
        """Render prediction as heatmap image."""
        if title is None:
            sim_note = " (SIMULATED)" if self.simulated else ""
            title = f"{self.channel} ({self.freq_mhz:.3f} MHz) Coverage from {self.tx_callsign} @ {self.utc_hour:02d}Z{sim_note}"
        
        # Update grid metadata for display
        self.grid.freq_mhz = self.freq_mhz
        self.grid.utc_hour = self.utc_hour
        self.grid.tx_lat = self.tx_lat
        self.grid.tx_lon = self.tx_lon
        
        return render_heatmap_with_basemap(
            self.grid,
            style=style,
            title=title,
            output_path=output_path
        )
    
    @property
    def best_coverage_percent(self) -> float:
        """Return percentage of grid with >50% reliability."""
        good_points = sum(1 for p in self.grid.points if p.reliability > 50)
        return 100.0 * good_points / len(self.grid.points)


class PropagationPredictor:
    """
    Main interface for propagation predictions.
    
    Handles VOACAP execution, or falls back to simulated data
    if voacapl is not available.
    """
    
    def __init__(
        self,
        callsign: str,
        lat: float,
        lon: float,
        antenna: Optional[AntennaConfig] = None,
        voacap_config: Optional[VoacapConfig] = None
    ):
        """
        Initialize predictor.
        
        Args:
            callsign: Station callsign
            lat: Latitude (positive North)
            lon: Longitude (positive East, negative West)
            antenna: Antenna configuration (default: dipole)
            voacap_config: voacapl configuration
        """
        self.station = StationConfig(
            callsign=callsign,
            lat=lat,
            lon=lon,
            antenna=antenna or AntennaConfig()
        )
        
        self.voacap_config = voacap_config or VoacapConfig.default()
        self.runner = VoacapRunner(self.voacap_config)
        
        # Check if voacapl is available
        self._voacapl_available = self.runner.is_available()
        if not self._voacapl_available:
            print(f"Warning: voacapl not available ({self.runner.get_error()})")
            print("Using simulated propagation data instead.")
    
    @property
    def voacapl_available(self) -> bool:
        """Check if voacapl is available for real predictions."""
        return self._voacapl_available
    
    def predict_coverage(
        self,
        channel: Optional[str] = None,
        freq_mhz: Optional[float] = None,
        utc_hour: Optional[int] = None,
        ssn: Optional[int] = None,
        sfi: Optional[float] = None,
        bounds: Optional[AreaBounds] = None,
        grid_size: int = 100
    ) -> PredictionResult:
        """
        Generate area coverage prediction.
        
        Args:
            channel: MARS channel ID (e.g., "A4")
            freq_mhz: Frequency in MHz (alternative to channel)
            utc_hour: UTC hour for prediction (default: current)
            ssn: Smoothed Sunspot Number
            sfi: Solar Flux Index (alternative to SSN)
            bounds: Geographic bounds (default: CONUS)
            grid_size: Grid resolution
            
        Returns:
            PredictionResult with coverage grid
        """
        # Resolve frequency
        if channel:
            freq = channel_to_freq(channel)
            if freq == 0.0:
                raise ValueError(f"Unknown channel: {channel}")
        elif freq_mhz:
            freq = freq_mhz
            channel = f"{freq_mhz:.3f} MHz"
        else:
            raise ValueError("Must specify channel or freq_mhz")
        
        # Resolve UTC hour
        if utc_hour is None:
            utc_hour = datetime.now(timezone.utc).hour
        
        # Resolve SSN
        if ssn is None:
            if sfi:
                ssn = sfi_to_ssn(sfi)
            else:
                ssn = 100  # Default moderate conditions
        
        # Resolve bounds
        if bounds is None:
            bounds = AreaBounds.conus()
        
        # Create propagation parameters
        params = PropagationParams(
            freq_mhz=freq,
            ssn=ssn,
            utc_hour=utc_hour,
            grid_size=grid_size
        )
        
        # Try real prediction with voacapl
        if self._voacapl_available:
            grid = self._run_real_prediction(params, bounds)
            simulated = False
        else:
            # Fall back to simulated data
            grid = self._generate_simulated(params, bounds, grid_size)
            simulated = True
        
        return PredictionResult(
            grid=grid,
            channel=channel,
            freq_mhz=freq,
            utc_hour=utc_hour,
            ssn=ssn,
            tx_callsign=self.station.callsign,
            tx_lat=self.station.lat,
            tx_lon=self.station.lon,
            bounds=bounds,
            simulated=simulated
        )
    
    def _run_real_prediction(
        self,
        params: PropagationParams,
        bounds: AreaBounds
    ) -> PropagationGrid:
        """Run actual voacapl prediction."""
        import tempfile
        
        # Generate ICE file
        with tempfile.NamedTemporaryFile(suffix=".ICE", delete=False) as f:
            ice_path = Path(f.name)
        
        generate_ice_file(self.station, params, bounds, output_path=ice_path)
        
        # Run voacapl
        output_path = self.runner.run_area_prediction(ice_path, verbose=False)
        
        if output_path is None:
            raise RuntimeError("voacapl prediction failed")
        
        # Parse output
        grid = parse_ig1_file(output_path, grid_size=params.grid_size)
        
        # Clean up
        ice_path.unlink(missing_ok=True)
        
        return grid
    
    def _generate_simulated(
        self,
        params: PropagationParams,
        bounds: AreaBounds,
        grid_size: int
    ) -> PropagationGrid:
        """Generate simulated propagation data."""
        return generate_sample_grid(
            tx_lat=self.station.lat,
            tx_lon=self.station.lon,
            bounds=(bounds.west, bounds.east, bounds.south, bounds.north),
            grid_size=grid_size,
            freq_mhz=params.freq_mhz,
            ssn=params.ssn
        )
    
    def compare_channels(
        self,
        channels: List[str],
        utc_hour: Optional[int] = None,
        ssn: Optional[int] = None,
        grid_size: int = 50,
        output_path: Optional[Path] = None
    ):
        """
        Compare coverage across multiple channels.
        
        Args:
            channels: List of channel IDs to compare
            utc_hour: UTC hour
            ssn: Smoothed Sunspot Number
            grid_size: Grid resolution (smaller for speed)
            output_path: Path to save comparison image
            
        Returns:
            Matplotlib figure
        """
        results = []
        
        for ch in channels:
            result = self.predict_coverage(
                channel=ch,
                utc_hour=utc_hour,
                ssn=ssn,
                grid_size=grid_size
            )
            
            # Update grid metadata
            result.grid.tx_lat = self.station.lat
            result.grid.tx_lon = self.station.lon
            
            sim = " (SIM)" if result.simulated else ""
            results.append((f"{ch} ({result.freq_mhz:.3f} MHz){sim}", result.grid))
        
        return render_multi_channel_comparison(results, output_path=output_path)


# Convenience function
def quick_coverage(
    callsign: str,
    lat: float,
    lon: float,
    channel: str,
    utc_hour: Optional[int] = None,
    sfi: float = 150.0,
    output_path: Optional[str] = None
) -> PredictionResult:
    """
    Quick one-liner for coverage prediction.
    
    Example:
        result = quick_coverage("AAR6XX", 38.47, -82.63, "A4", output_path="a4.png")
    """
    pred = PropagationPredictor(callsign, lat, lon)
    result = pred.predict_coverage(
        channel=channel,
        utc_hour=utc_hour,
        sfi=sfi
    )
    
    if output_path:
        result.render_heatmap(Path(output_path))
    
    return result


if __name__ == "__main__":
    print("=" * 60)
    print("Phoenix Nest Propagation Prediction Module")
    print("=" * 60)
    
    # Check voacapl installation
    print("\nChecking voacapl installation...")
    available, msg = check_voacapl_installation()
    print(f"  {msg}")
    
    if not available:
        print("\n  Note: Using SIMULATED propagation data")
        print("  Install voacapl for real predictions")
    
    # Example prediction
    print("\n" + "-" * 60)
    print("Example: Coverage prediction from Eastern Kentucky")
    print("-" * 60)
    
    # Create predictor
    pred = PropagationPredictor(
        callsign="AAR6XX",
        lat=38.47,    # Ashland, KY area
        lon=-82.63
    )
    
    # Single channel prediction
    print("\nGenerating A4 coverage prediction...")
    result = pred.predict_coverage(
        channel="A4",
        utc_hour=18,
        sfi=150,
        grid_size=50
    )
    
    output_dir = Path(__file__).parent / "examples"
    output_dir.mkdir(exist_ok=True)
    
    result.render_heatmap(output_dir / "a4_coverage.png")
    print(f"  Coverage >50%: {result.best_coverage_percent:.1f}% of CONUS")
    
    # Channel comparison
    print("\nGenerating channel comparison...")
    pred.compare_channels(
        channels=["A4", "A8", "A12"],
        utc_hour=18,
        ssn=100,
        grid_size=30,
        output_path=output_dir / "mars_comparison.png"
    )
    
    print(f"\nOutput saved to: {output_dir}/")
    print("\nDone!")
