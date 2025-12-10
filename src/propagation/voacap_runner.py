"""
VOACAP/voacapl execution wrapper.

Handles running the voacapl propagation engine and managing input/output files.
"""

import subprocess
import shutil
from pathlib import Path
from typing import Optional, Tuple
from dataclasses import dataclass
import os
import tempfile


@dataclass
class VoacapConfig:
    """voacapl configuration."""
    itshfbc_path: Path        # Path to itshfbc directory
    voacapl_binary: str       # Name or path to voacapl binary
    timeout_seconds: int = 60  # Execution timeout
    
    @classmethod
    def default(cls) -> 'VoacapConfig':
        """Create default configuration."""
        return cls(
            itshfbc_path=Path.home() / "itshfbc",
            voacapl_binary="voacapl"
        )
    
    def validate(self) -> Tuple[bool, str]:
        """Check if configuration is valid."""
        # Check itshfbc directory
        if not self.itshfbc_path.exists():
            return False, f"itshfbc directory not found: {self.itshfbc_path}"
        
        # Check for required subdirectories
        required_dirs = ["run", "areadata", "antennas", "coeffs"]
        for dir_name in required_dirs:
            if not (self.itshfbc_path / dir_name).exists():
                return False, f"Required directory missing: {self.itshfbc_path / dir_name}"
        
        # Check for voacapl binary
        if shutil.which(self.voacapl_binary) is None:
            # Try as absolute path
            if not Path(self.voacapl_binary).exists():
                return False, f"voacapl binary not found: {self.voacapl_binary}"
        
        return True, "Configuration valid"


class VoacapRunner:
    """
    Runs voacapl predictions.
    
    Example usage:
        runner = VoacapRunner()
        if runner.is_available():
            result = runner.run_area_prediction(ice_file_path)
    """
    
    def __init__(self, config: Optional[VoacapConfig] = None):
        """Initialize runner with configuration."""
        self.config = config or VoacapConfig.default()
        self._validated = False
        self._validation_error = ""
    
    def is_available(self) -> bool:
        """Check if voacapl is available and configured."""
        valid, error = self.config.validate()
        self._validated = valid
        self._validation_error = error
        return valid
    
    def get_error(self) -> str:
        """Get last validation error."""
        return self._validation_error
    
    def run_area_prediction(
        self,
        ice_file: Path,
        output_dir: Optional[Path] = None,
        verbose: bool = False
    ) -> Optional[Path]:
        """
        Run area coverage prediction.
        
        Args:
            ice_file: Path to .ICE input file
            output_dir: Directory for output files (default: itshfbc/areadata/default)
            verbose: Print voacapl output
            
        Returns:
            Path to .ig1 output file, or None on failure
        """
        ice_file = Path(ice_file)
        if not ice_file.exists():
            raise FileNotFoundError(f"ICE file not found: {ice_file}")
        
        # Set up output directory
        if output_dir is None:
            output_dir = self.config.itshfbc_path / "areadata" / "default"
        output_dir.mkdir(parents=True, exist_ok=True)
        
        # Copy ICE file to proper location
        dest_ice = output_dir / ice_file.name
        if ice_file != dest_ice:
            shutil.copy(ice_file, dest_ice)
        
        # Build command
        # voacapl usage: voacapl <itshfbc_path> AREA CALC <subdir/filename.ICE>
        relative_ice = f"default/{ice_file.name}"
        
        cmd = [
            self.config.voacapl_binary,
            str(self.config.itshfbc_path),
            "AREA",
            "CALC",
            relative_ice
        ]
        
        if verbose:
            print(f"Running: {' '.join(cmd)}")
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=self.config.timeout_seconds,
                cwd=str(self.config.itshfbc_path)
            )
            
            if verbose:
                print(f"stdout: {result.stdout}")
                if result.stderr:
                    print(f"stderr: {result.stderr}")
            
            if result.returncode != 0:
                print(f"voacapl failed with return code {result.returncode}")
                return None
            
        except subprocess.TimeoutExpired:
            print(f"voacapl timed out after {self.config.timeout_seconds}s")
            return None
        except FileNotFoundError:
            print(f"voacapl binary not found: {self.config.voacapl_binary}")
            return None
        
        # Find output file
        # Output is typically named like input but with .ig1 extension
        output_file = output_dir / ice_file.name.replace(".ICE", ".ig1")
        
        # Or try pattern 1.ig1
        if not output_file.exists():
            output_file = output_dir / "1.ig1"
        
        if output_file.exists():
            return output_file
        
        # Search for any .ig1 file
        ig1_files = list(output_dir.glob("*.ig1"))
        if ig1_files:
            return ig1_files[0]
        
        print("No output file found")
        return None
    
    def run_p2p_prediction(
        self,
        dat_file: Path,
        verbose: bool = False
    ) -> Optional[Path]:
        """
        Run point-to-point prediction.
        
        Args:
            dat_file: Path to .DAT input file
            verbose: Print voacapl output
            
        Returns:
            Path to .OUT output file, or None on failure
        """
        dat_file = Path(dat_file)
        if not dat_file.exists():
            raise FileNotFoundError(f"DAT file not found: {dat_file}")
        
        run_dir = self.config.itshfbc_path / "run"
        
        # Copy to run directory if needed
        dest_dat = run_dir / dat_file.name
        if dat_file != dest_dat:
            shutil.copy(dat_file, dest_dat)
        
        # voacapl usage for P2P: voacapl <itshfbc_path>
        # It reads voacapx.dat from run directory by default
        
        # Rename to voacapx.dat if needed
        voacapx = run_dir / "voacapx.dat"
        if dat_file.name != "voacapx.dat":
            shutil.copy(dest_dat, voacapx)
        
        cmd = [
            self.config.voacapl_binary,
            str(self.config.itshfbc_path)
        ]
        
        if verbose:
            print(f"Running: {' '.join(cmd)}")
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=self.config.timeout_seconds,
                cwd=str(run_dir)
            )
            
            if verbose:
                print(f"stdout: {result.stdout}")
                if result.stderr:
                    print(f"stderr: {result.stderr}")
            
            if result.returncode != 0:
                print(f"voacapl failed with return code {result.returncode}")
                return None
            
        except subprocess.TimeoutExpired:
            print(f"voacapl timed out after {self.config.timeout_seconds}s")
            return None
        except FileNotFoundError:
            print(f"voacapl binary not found: {self.config.voacapl_binary}")
            return None
        
        # Output file
        output_file = run_dir / "voacapx.out"
        if output_file.exists():
            return output_file
        
        return None
    
    def cleanup_temp_files(self):
        """Remove temporary files from itshfbc directories."""
        patterns = ["*.ig1", "*.out", "voacapx.dat", "*.ICE"]
        
        # Clean run directory
        run_dir = self.config.itshfbc_path / "run"
        for pattern in patterns:
            for f in run_dir.glob(pattern):
                f.unlink()
        
        # Clean areadata/default
        area_dir = self.config.itshfbc_path / "areadata" / "default"
        for pattern in patterns:
            for f in area_dir.glob(pattern):
                f.unlink()


def check_voacapl_installation() -> Tuple[bool, str]:
    """
    Check if voacapl is properly installed.
    
    Returns:
        Tuple of (success, message)
    """
    # Check for binary
    voacapl_path = shutil.which("voacapl")
    if not voacapl_path:
        return False, "voacapl binary not found in PATH"
    
    # Check for itshfbc
    itshfbc = Path.home() / "itshfbc"
    if not itshfbc.exists():
        return False, f"itshfbc directory not found at {itshfbc}"
    
    # Check structure
    required = ["run", "areadata", "antennas/samples", "coeffs/ccir"]
    for subdir in required:
        if not (itshfbc / subdir).exists():
            return False, f"Missing directory: {itshfbc / subdir}"
    
    return True, f"voacapl installed at {voacapl_path}, data at {itshfbc}"


if __name__ == "__main__":
    print("=== voacapl Installation Check ===\n")
    
    success, message = check_voacapl_installation()
    
    if success:
        print(f"✓ {message}")
    else:
        print(f"✗ {message}")
        print("\nTo install voacapl on Linux:")
        print("  1. Install gfortran: sudo apt install build-essential gfortran")
        print("  2. Download: https://github.com/jawatson/voacapl/releases")
        print("  3. Build: ./configure && make && sudo make install")
        print("  4. Initialize: makeitshfbc")
        print("\nSee: https://www.qsl.net/hz1jw/voacapl/")
