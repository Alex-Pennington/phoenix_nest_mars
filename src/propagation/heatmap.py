"""
Propagation heatmap visualization.

Renders VOACAP area predictions as colored overlays on geographic maps.
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.patches import Rectangle
from matplotlib.collections import PatchCollection
from dataclasses import dataclass
from typing import Optional, Tuple, List
from pathlib import Path

from ig1_parser import PropagationGrid, GridPoint


@dataclass
class HeatmapStyle:
    """Visual style for heatmap rendering."""
    colormap: str = "RdYlGn"    # Red (bad) -> Yellow -> Green (good)
    alpha: float = 0.6          # Transparency
    min_val: float = 0.0        # Minimum reliability to show
    max_val: float = 100.0      # Maximum reliability
    edge_color: str = 'none'    # Grid cell edge color
    show_colorbar: bool = True


def create_reliability_colormap() -> mcolors.LinearSegmentedColormap:
    """
    Create a custom colormap for propagation reliability.
    
    Colors:
        0-20%   : Red (poor)
        20-40%  : Orange 
        40-60%  : Yellow
        60-80%  : Light Green
        80-100% : Green (excellent)
    """
    colors = [
        (0.8, 0.0, 0.0),   # Red
        (1.0, 0.4, 0.0),   # Orange
        (1.0, 1.0, 0.0),   # Yellow
        (0.6, 1.0, 0.3),   # Light Green
        (0.0, 0.7, 0.0),   # Green
    ]
    
    return mcolors.LinearSegmentedColormap.from_list("propagation", colors)


def render_heatmap(
    grid: PropagationGrid,
    ax: Optional[plt.Axes] = None,
    style: Optional[HeatmapStyle] = None,
    title: Optional[str] = None,
    show_tx: bool = True
) -> plt.Axes:
    """
    Render propagation grid as a heatmap.
    
    Args:
        grid: PropagationGrid to visualize
        ax: Matplotlib axes (created if None)
        style: Visual style settings
        title: Plot title
        show_tx: Whether to show transmitter location
        
    Returns:
        Matplotlib axes
    """
    if style is None:
        style = HeatmapStyle()
    
    if ax is None:
        fig, ax = plt.subplots(figsize=(12, 8))
    
    # Convert grid to 2D arrays
    lat_grid, lon_grid, rel_grid = grid.to_grid_array()
    
    # Create custom colormap
    cmap = create_reliability_colormap()
    
    # Plot as pcolormesh (faster than individual rectangles)
    mesh = ax.pcolormesh(
        lon_grid, lat_grid, rel_grid,
        cmap=cmap,
        alpha=style.alpha,
        vmin=style.min_val,
        vmax=style.max_val,
        shading='auto'
    )
    
    # Add colorbar
    if style.show_colorbar:
        cbar = plt.colorbar(mesh, ax=ax, shrink=0.8, pad=0.02)
        cbar.set_label('Circuit Reliability (%)', fontsize=10)
    
    # Mark transmitter location
    if show_tx and grid.tx_lat != 0 and grid.tx_lon != 0:
        ax.plot(grid.tx_lon, grid.tx_lat, 'k^', markersize=12, 
                markerfacecolor='yellow', markeredgecolor='black',
                markeredgewidth=2, label='TX', zorder=10)
    
    # Set labels
    ax.set_xlabel('Longitude', fontsize=10)
    ax.set_ylabel('Latitude', fontsize=10)
    
    if title:
        ax.set_title(title, fontsize=12, fontweight='bold')
    elif grid.freq_mhz > 0:
        ax.set_title(f'Propagation Coverage - {grid.freq_mhz:.3f} MHz @ {grid.utc_hour:02d}Z',
                     fontsize=12)
    
    # Set aspect ratio for geographic coordinates
    ax.set_aspect('auto')
    
    return ax


def render_heatmap_with_basemap(
    grid: PropagationGrid,
    style: Optional[HeatmapStyle] = None,
    title: Optional[str] = None,
    output_path: Optional[Path] = None,
    dpi: int = 150
) -> plt.Figure:
    """
    Render heatmap with simple geographic context.
    
    This version adds basic map features without requiring basemap/cartopy.
    
    Args:
        grid: PropagationGrid to visualize
        style: Visual style settings
        title: Plot title
        output_path: Path to save image
        dpi: Resolution for saved image
        
    Returns:
        Matplotlib Figure
    """
    if style is None:
        style = HeatmapStyle()
    
    fig, ax = plt.subplots(figsize=(14, 10))
    
    # Add basic geographic context
    # US state boundaries (simplified - would need shapefile for real version)
    _add_us_outline(ax)
    
    # Render heatmap
    render_heatmap(grid, ax=ax, style=style, title=title)
    
    # Set bounds with some padding
    ax.set_xlim(grid.min_lon - 2, grid.max_lon + 2)
    ax.set_ylim(grid.min_lat - 2, grid.max_lat + 2)
    
    # Add grid lines
    ax.grid(True, alpha=0.3, linestyle='--')
    
    # Add legend for transmitter
    ax.legend(loc='lower right', fontsize=9)
    
    plt.tight_layout()
    
    if output_path:
        fig.savefig(output_path, dpi=dpi, bbox_inches='tight',
                    facecolor='white', edgecolor='none')
        print(f"Saved to: {output_path}")
    
    return fig


def _add_us_outline(ax: plt.Axes):
    """
    Add simplified US outline to plot.
    
    This is a rough approximation - real implementation would use shapefiles.
    """
    # Very simplified CONUS outline
    us_outline = [
        (-124.7, 48.4), (-123.0, 48.4), (-123.0, 46.0), (-124.5, 43.0),
        (-124.5, 40.0), (-120.0, 34.0), (-117.0, 32.5), (-114.5, 32.5),
        (-111.0, 31.3), (-108.0, 31.3), (-106.5, 31.8), (-104.0, 29.5),
        (-103.0, 29.0), (-102.0, 29.8), (-99.0, 26.0), (-97.0, 26.0),
        (-97.0, 28.0), (-94.0, 29.5), (-91.0, 29.0), (-89.0, 29.2),
        (-88.5, 30.2), (-87.5, 30.3), (-86.5, 30.4), (-85.0, 29.7),
        (-83.0, 29.0), (-81.0, 25.0), (-80.0, 25.0), (-80.0, 31.0),
        (-81.0, 35.0), (-75.5, 35.5), (-75.5, 37.0), (-76.0, 38.0),
        (-75.0, 39.5), (-74.0, 40.0), (-74.0, 41.0), (-72.0, 41.0),
        (-71.0, 42.0), (-70.0, 42.0), (-70.0, 43.5), (-67.0, 44.5),
        (-67.0, 47.0), (-69.0, 47.4), (-71.0, 45.0), (-75.0, 45.0),
        (-79.0, 43.0), (-79.0, 42.0), (-83.0, 42.0), (-83.0, 46.5),
        (-88.0, 48.0), (-90.0, 48.0), (-95.0, 49.0), (-123.0, 49.0),
        (-124.7, 48.4)
    ]
    
    lons, lats = zip(*us_outline)
    ax.plot(lons, lats, 'k-', linewidth=1.5, alpha=0.7)


def render_multi_channel_comparison(
    grids: List[Tuple[str, PropagationGrid]],
    style: Optional[HeatmapStyle] = None,
    output_path: Optional[Path] = None,
    dpi: int = 150
) -> plt.Figure:
    """
    Render multiple channel predictions side-by-side for comparison.
    
    Args:
        grids: List of (channel_name, grid) tuples
        style: Visual style settings
        output_path: Path to save image
        dpi: Resolution for saved image
        
    Returns:
        Matplotlib Figure
    """
    n_grids = len(grids)
    cols = min(3, n_grids)
    rows = (n_grids + cols - 1) // cols
    
    fig, axes = plt.subplots(rows, cols, figsize=(6*cols, 5*rows))
    if n_grids == 1:
        axes = [axes]
    else:
        axes = axes.flatten()
    
    if style is None:
        style = HeatmapStyle(show_colorbar=False)
    
    for i, (name, grid) in enumerate(grids):
        ax = axes[i]
        _add_us_outline(ax)
        render_heatmap(grid, ax=ax, style=style, title=name)
        ax.set_xlim(grid.min_lon - 2, grid.max_lon + 2)
        ax.set_ylim(grid.min_lat - 2, grid.max_lat + 2)
        ax.grid(True, alpha=0.2)
    
    # Hide unused axes
    for i in range(n_grids, len(axes)):
        axes[i].set_visible(False)
    
    # Add single colorbar for all
    cmap = create_reliability_colormap()
    norm = mcolors.Normalize(vmin=0, vmax=100)
    sm = plt.cm.ScalarMappable(cmap=cmap, norm=norm)
    cbar = fig.colorbar(sm, ax=axes, shrink=0.6, pad=0.02, 
                        label='Circuit Reliability (%)')
    
    plt.suptitle('Channel Comparison', fontsize=14, fontweight='bold', y=1.02)
    plt.tight_layout()
    
    if output_path:
        fig.savefig(output_path, dpi=dpi, bbox_inches='tight',
                    facecolor='white')
        print(f"Saved to: {output_path}")
    
    return fig


if __name__ == "__main__":
    print("=== Heatmap Visualization Test ===\n")
    
    # Import generator for test data
    from ig1_parser import generate_sample_grid
    
    # Generate sample propagation grid
    grid = generate_sample_grid(
        tx_lat=38.47,   # Ashland, KY area
        tx_lon=-82.63,
        bounds=(-126.0, -66.0, 24.0, 50.0),  # CONUS
        grid_size=50,
        freq_mhz=5.330,  # A4 channel
        ssn=100
    )
    
    # Update grid metadata
    grid.tx_lat = 38.47
    grid.tx_lon = -82.63
    grid.freq_mhz = 5.330
    grid.utc_hour = 18
    
    print(f"Grid: {len(grid.points)} points")
    
    # Render heatmap
    output_dir = Path(__file__).parent / "examples"
    output_dir.mkdir(exist_ok=True)
    
    fig = render_heatmap_with_basemap(
        grid,
        title="A4 (5.330 MHz) Coverage from Eastern KY @ 18Z",
        output_path=output_dir / "sample_heatmap.png"
    )
    
    # Generate comparison
    print("\nGenerating channel comparison...")
    
    channels = [
        ("A4 (5.330 MHz)", generate_sample_grid(freq_mhz=5.330, ssn=100, grid_size=30)),
        ("A8 (10.145 MHz)", generate_sample_grid(freq_mhz=10.145, ssn=100, grid_size=30)),
        ("A12 (14.870 MHz)", generate_sample_grid(freq_mhz=14.870, ssn=100, grid_size=30)),
    ]
    
    # Set TX location for all
    for name, g in channels:
        g.tx_lat = 38.47
        g.tx_lon = -82.63
        g.utc_hour = 18
    
    fig2 = render_multi_channel_comparison(
        channels,
        output_path=output_dir / "channel_comparison.png"
    )
    
    print("\nDone! Check examples/ directory for output images.")
    
    # Show plots if running interactively
    plt.show()
