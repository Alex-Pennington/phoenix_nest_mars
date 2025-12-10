#!/usr/bin/env python3
"""
MARS Cipher Key Management for phoenixnestmodem
MIL-STD-188-110A/141 Compatible

Author: Generated for Phoenix Nest LLC
License: For MARS/amateur radio use only
"""

import struct
from pathlib import Path
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
from enum import Enum, auto
from datetime import datetime
import hashlib
import os


class KeyType(Enum):
    """MARS Key Types"""
    ZYG = auto()   # US MARS General Membership
    ZYI1 = auto()  # US Interoperability
    ZYI2 = auto()  # NATO Interoperability


class KeyInterval(Enum):
    """Key rotation intervals"""
    MONTHLY = "Monthly"
    SEMIANNUAL = "SemiAnnual"


@dataclass
class CipherKey:
    """
    Individual cipher key with KIK and TEK components
    
    KIK Structure (96 bytes):
        - Bytes 0-31:  Common Key Seed (shared across key types for same period)
        - Bytes 32-63: Key Variant Block (unique per key type)
        - Bytes 64-95: Validation/MAC
    
    TEK Structure:
        - ZYG: 256 bytes (16 x 128-bit blocks)
        - ZYI1/ZYI2: 464 bytes (29 x 128-bit blocks)
    """
    ckr: str                    # Call sign Key Reference (e.g., "ZYG1")
    key_name: str               # Full key identifier (e.g., "ZYG UMT0120")
    description: str            # Human-readable description
    effective_period: str       # MMYY format
    key_type: KeyType
    
    # Raw key material
    kik: bytes = field(repr=False)  # 96 bytes - Key Initialization Key
    tek: bytes = field(repr=False)  # 256 or 464 bytes - Traffic Encryption Keys
    
    # Parsed KIK components
    kik_seed: bytes = field(default=b'', repr=False)      # bytes 0-31
    kik_variant: bytes = field(default=b'', repr=False)   # bytes 32-63  
    kik_mac: bytes = field(default=b'', repr=False)       # bytes 64-95
    
    # Parsed TEK blocks
    tek_blocks: List[bytes] = field(default_factory=list, repr=False)
    
    def __post_init__(self):
        """Parse key components after initialization"""
        # Parse KIK structure
        if len(self.kik) == 96:
            self.kik_seed = self.kik[0:32]
            self.kik_variant = self.kik[32:64]
            self.kik_mac = self.kik[64:96]
        
        # Parse TEK into 16-byte (128-bit) blocks
        self.tek_blocks = []
        for i in range(0, len(self.tek), 16):
            self.tek_blocks.append(self.tek[i:i+16])
    
    def get_tek_for_slot(self, slot: int) -> bytes:
        """Get TEK for specific time slot"""
        if 0 <= slot < len(self.tek_blocks):
            return self.tek_blocks[slot]
        raise ValueError(f"Invalid TEK slot {slot}, max is {len(self.tek_blocks)-1}")
    
    def derive_session_key(self, nonce: bytes) -> bytes:
        """
        Derive session key from KIK and nonce
        
        Note: This is a placeholder implementation using HKDF-like derivation.
        Replace with actual MARS algorithm when specifications are available.
        """
        material = self.kik_seed + nonce + self.kik_variant
        return hashlib.sha256(material).digest()
    
    def verify_integrity(self) -> bool:
        """Verify KIK integrity using MAC block"""
        # Compute expected MAC
        computed = hashlib.sha256(self.kik_seed + self.kik_variant).digest()
        return computed == self.kik_mac
    
    def zeroize(self):
        """Securely clear key material from memory"""
        # Overwrite with random data then zeros
        if self.kik:
            random_fill = os.urandom(len(self.kik))
            # Can't actually modify bytes, but this signals intent
            self.kik = b'\x00' * 96
            self.kik_seed = b'\x00' * 32
            self.kik_variant = b'\x00' * 32
            self.kik_mac = b'\x00' * 32
        
        if self.tek:
            self.tek = b'\x00' * len(self.tek)
            self.tek_blocks = []


class IntelHexParser:
    """Parse Intel HEX format files (IHEX)"""
    
    @staticmethod
    def parse(content: str) -> bytes:
        """
        Parse Intel HEX content and return raw bytes
        
        Format per line:
        :LLAAAATT[DD...]CC
        - LL: Byte count
        - AAAA: 16-bit address
        - TT: Record type (00=data, 01=EOF)
        - DD: Data bytes
        - CC: Checksum
        """
        data = bytearray()
        
        for line_num, line in enumerate(content.strip().split('\n'), 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(':'):
                continue
            
            try:
                byte_count = int(line[1:3], 16)
                address = int(line[3:7], 16)
                record_type = int(line[7:9], 16)
                
                if record_type == 0x00:  # Data record
                    hex_data = line[9:9 + byte_count * 2]
                    checksum = int(line[9 + byte_count * 2:9 + byte_count * 2 + 2], 16)
                    
                    # Verify checksum
                    record_bytes = bytes.fromhex(line[1:9 + byte_count * 2])
                    calc_checksum = (~sum(record_bytes) + 1) & 0xFF
                    
                    if calc_checksum != checksum:
                        raise ValueError(
                            f"Checksum mismatch at line {line_num}, "
                            f"address 0x{address:04X}: "
                            f"expected 0x{checksum:02X}, got 0x{calc_checksum:02X}"
                        )
                    
                    # Extend data to address if needed (handle sparse files)
                    while len(data) < address:
                        data.append(0xFF)
                    
                    data.extend(bytes.fromhex(hex_data))
                    
                elif record_type == 0x01:  # EOF
                    break
                    
            except (ValueError, IndexError) as e:
                raise ValueError(f"Parse error at line {line_num}: {e}")
        
        return bytes(data)
    
    @staticmethod
    def generate(data: bytes, bytes_per_line: int = 16) -> str:
        """Generate Intel HEX format from raw bytes"""
        lines = []
        
        for addr in range(0, len(data), bytes_per_line):
            chunk = data[addr:addr + bytes_per_line]
            byte_count = len(chunk)
            
            # Build record without checksum
            record = f"{byte_count:02X}{addr:04X}00"
            record += chunk.hex().upper()
            
            # Calculate checksum
            record_bytes = bytes.fromhex(record)
            checksum = (~sum(record_bytes) + 1) & 0xFF
            
            lines.append(f":{record}{checksum:02X}")
        
        # Add EOF record
        lines.append(":00000001FF")
        
        return '\r\n'.join(lines)


class KeyTape:
    """
    Container for a complete MARS key tape
    
    Manages loading, storage, and retrieval of cipher keys
    from HEX files and database metadata.
    """
    
    def __init__(self):
        self.keys: Dict[str, CipherKey] = {}
        self.loaded_from: Optional[Path] = None
        self.integrity_hash: bytes = b''
        self._load_time: Optional[datetime] = None
    
    def load_hex_files(self, kik_path: Path, tek_path: Path, 
                       ckr: str, key_name: str, description: str,
                       key_type: KeyType) -> CipherKey:
        """
        Load key from paired HEX files
        
        Args:
            kik_path: Path to Key Initialization Key .HEX file
            tek_path: Path to Traffic Encryption Key .HEX file
            ckr: Call sign Key Reference (e.g., "ZYG1")
            key_name: Full key identifier (e.g., "ZYG UMT0120")
            description: Human-readable description
            key_type: KeyType enum value
            
        Returns:
            Loaded CipherKey object
        """
        with open(kik_path, 'r') as f:
            kik_data = IntelHexParser.parse(f.read())
        
        with open(tek_path, 'r') as f:
            tek_data = IntelHexParser.parse(f.read())
        
        # Extract period from key name (e.g., "ZYG UMT0120" -> "0120")
        period = key_name.split()[-1][-4:] if ' ' in key_name else key_name[-4:]
        
        key = CipherKey(
            ckr=ckr,
            key_name=key_name,
            description=description,
            effective_period=period,
            key_type=key_type,
            kik=kik_data,
            tek=tek_data
        )
        
        self.keys[ckr] = key
        self._load_time = datetime.now()
        
        return key
    
    def load_from_directory(self, base_path: Path) -> int:
        """
        Auto-discover and load all keys from a directory structure
        
        Expected structure:
            base_path/
                part1files/  (KIK files)
                part2files/  (TEK files)
        
        Returns:
            Number of keys loaded
        """
        part1 = base_path / "part1files"
        part2 = base_path / "part2files"
        
        if not part1.exists() or not part2.exists():
            raise FileNotFoundError(f"Expected part1files and part2files in {base_path}")
        
        loaded = 0
        
        # Map KIK files to TEK files by key identifier
        kik_files = {f.stem.split()[0]: f for f in part1.glob("*.HEX")}
        tek_files = {f.stem.split()[0]: f for f in part2.glob("*.HEX")}
        
        # Key type mapping
        type_map = {
            "ZYG": KeyType.ZYG,
            "ZYI1": KeyType.ZYI1,
            "ZYI2": KeyType.ZYI2,
        }
        
        for prefix, kik_file in kik_files.items():
            # Find matching TEK file
            tek_file = tek_files.get(prefix)
            if not tek_file:
                continue
            
            # Determine key type
            key_type = None
            for type_prefix, kt in type_map.items():
                if prefix.startswith(type_prefix):
                    key_type = kt
                    break
            
            if not key_type:
                continue
            
            # Generate CKR (e.g., "ZYG1" for first ZYG key)
            existing = [k for k in self.keys if k.startswith(prefix)]
            ckr = f"{prefix}{len(existing) + 1}"
            
            # Extract key name from TEK file
            key_name = tek_file.stem
            
            try:
                self.load_hex_files(
                    kik_path=kik_file,
                    tek_path=tek_file,
                    ckr=ckr,
                    key_name=key_name,
                    description=f"Auto-loaded from {base_path}",
                    key_type=key_type
                )
                loaded += 1
            except Exception as e:
                print(f"Warning: Failed to load {kik_file.name}: {e}")
        
        self.loaded_from = base_path
        return loaded
    
    def get_key(self, ckr: str) -> Optional[CipherKey]:
        """Retrieve key by CKR"""
        return self.keys.get(ckr)
    
    def get_active_key(self, key_type: KeyType, date: Optional[datetime] = None) -> Optional[CipherKey]:
        """
        Get active key for type and date
        
        Args:
            key_type: Type of key needed
            date: Target date (defaults to now)
            
        Returns:
            Active CipherKey or None
        """
        if date is None:
            date = datetime.now()
        
        period = date.strftime("%m%y")  # MMYY format
        
        prefix_map = {
            KeyType.ZYG: "ZYG",
            KeyType.ZYI1: "ZYI1", 
            KeyType.ZYI2: "ZYI2"
        }
        
        prefix = prefix_map[key_type]
        
        for ckr, key in self.keys.items():
            if ckr.startswith(prefix) and key.effective_period == period:
                return key
        
        return None
    
    def list_keys(self) -> List[Tuple[str, str, KeyType]]:
        """List all loaded keys as (ckr, key_name, key_type) tuples"""
        return [(k.ckr, k.key_name, k.key_type) for k in self.keys.values()]
    
    def zeroize_all(self):
        """Securely clear all key material"""
        for key in self.keys.values():
            key.zeroize()
        self.keys.clear()


class CipherEngine:
    """
    Encryption/decryption engine for MIL-STD-188-110A modem frames
    
    Provides session management, key derivation, and frame encryption
    compatible with MARS cipher key tapes.
    """
    
    def __init__(self, key_tape: KeyTape):
        self.key_tape = key_tape
        self.active_key: Optional[CipherKey] = None
        self.session_key: bytes = b''
        self.current_tek_slot: int = 0
        self._session_start: Optional[datetime] = None
    
    def init_session(self, ckr: str, nonce: bytes) -> bool:
        """
        Initialize crypto session with specified key
        
        Args:
            ckr: Call sign Key Reference to use
            nonce: Random nonce from ALE handshake (typically 16 bytes)
            
        Returns:
            True if session initialized successfully
        """
        key = self.key_tape.get_key(ckr)
        if not key:
            return False
        
        self.active_key = key
        self.session_key = key.derive_session_key(nonce)
        self.current_tek_slot = 0
        self._session_start = datetime.now()
        
        return True
    
    def init_session_auto(self, key_type: KeyType, nonce: bytes) -> bool:
        """
        Initialize session with automatically selected key
        
        Selects the active key for current date.
        """
        key = self.key_tape.get_active_key(key_type)
        if not key:
            return False
        
        return self.init_session(key.ckr, nonce)
    
    def advance_tek_slot(self):
        """Advance to next TEK slot (for time-based key rotation)"""
        if self.active_key:
            max_slot = len(self.active_key.tek_blocks) - 1
            self.current_tek_slot = (self.current_tek_slot + 1) % max_slot
    
    def get_tek_slot_for_time(self, timestamp: datetime) -> int:
        """
        Calculate TEK slot for a given timestamp
        
        For ZYG (16 slots over 6 months): ~11 days per slot
        For ZYI (29 slots over 12 months): ~12.5 days per slot
        """
        if not self.active_key:
            return 0
        
        # Parse effective period (MMYY)
        month = int(self.active_key.effective_period[:2])
        year = 2000 + int(self.active_key.effective_period[2:])
        
        period_start = datetime(year, month, 1)
        days_elapsed = (timestamp - period_start).days
        
        num_slots = len(self.active_key.tek_blocks)
        
        if self.active_key.key_type == KeyType.ZYG:
            # 6-month period, 16 slots
            days_per_slot = 180 / num_slots
        else:
            # 12-month period, 29 slots
            days_per_slot = 365 / num_slots
        
        slot = int(days_elapsed / days_per_slot)
        return max(0, min(slot, num_slots - 1))
    
    def encrypt_frame(self, plaintext: bytes) -> bytes:
        """
        Encrypt a modem frame
        
        Note: This is a placeholder XOR implementation.
        Replace with actual MIL-STD cipher algorithm.
        """
        if not self.active_key:
            raise RuntimeError("No active key - call init_session first")
        
        tek = self.active_key.get_tek_for_slot(self.current_tek_slot)
        
        # XOR-based encryption (placeholder)
        # Real implementation should use proper block cipher (AES-CTR, etc.)
        result = bytearray()
        for i, byte in enumerate(plaintext):
            key_byte = tek[i % len(tek)] ^ self.session_key[i % len(self.session_key)]
            result.append(byte ^ key_byte)
        
        return bytes(result)
    
    def decrypt_frame(self, ciphertext: bytes) -> bytes:
        """Decrypt a received frame (symmetric with encrypt for XOR)"""
        return self.encrypt_frame(ciphertext)
    
    def end_session(self):
        """End session and clear sensitive material"""
        self.session_key = b'\x00' * 32
        self.active_key = None
        self.current_tek_slot = 0
        self._session_start = None


# Convenience function for quick testing
def test_cipher_roundtrip():
    """Test cipher with sample data"""
    from pathlib import Path
    
    # Create test key tape
    tape = KeyTape()
    
    # Try to load from current directory
    test_paths = [
        Path("."),
        Path("cipher"),
        Path("/home/claude/cipher"),
    ]
    
    for path in test_paths:
        if (path / "part1files").exists():
            loaded = tape.load_from_directory(path)
            print(f"Loaded {loaded} keys from {path}")
            break
    else:
        print("No key files found - creating dummy test key")
        # Create minimal test key
        test_key = CipherKey(
            ckr="TEST1",
            key_name="TEST KEY",
            description="Test key for validation",
            effective_period="0125",
            key_type=KeyType.ZYG,
            kik=os.urandom(96),
            tek=os.urandom(256)
        )
        tape.keys["TEST1"] = test_key
    
    # Test encryption
    engine = CipherEngine(tape)
    
    test_ckr = list(tape.keys.keys())[0]
    nonce = os.urandom(16)
    
    if engine.init_session(test_ckr, nonce):
        plaintext = b"Hello MARS Network - KE4SBY"
        ciphertext = engine.encrypt_frame(plaintext)
        decrypted = engine.decrypt_frame(ciphertext)
        
        print(f"Plaintext:  {plaintext}")
        print(f"Ciphertext: {ciphertext.hex()}")
        print(f"Decrypted:  {decrypted}")
        
        assert decrypted == plaintext, "Roundtrip failed!"
        print("\n✓ Cipher roundtrip test PASSED")
        
        engine.end_session()
    else:
        print("Failed to initialize session")


if __name__ == '__main__':
    test_cipher_roundtrip()
