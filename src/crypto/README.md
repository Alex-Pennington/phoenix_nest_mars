# Phoenix Nest Crypto Module

MARS key management and encryption infrastructure for MS-DMT secure communications.

## Overview

This module provides key tape management and cipher operations for MARS digital traffic encryption. It handles the hierarchical key structure used in MS-DMT communications:

- **KIK** (Key Encryption Key) - Master keys for key distribution
- **TEK** (Traffic Encryption Key) - Session keys derived from KIK for actual traffic encryption

## Key Structure Discovery

Analysis of MARS key files revealed:

### KIK Architecture
The three key types (ZYG, ZYI1, ZYI2) share an **identical 32-byte seed** in the first block, confirming a hierarchical key derivation architecture where a master key generates all period-specific keys. The variant block (bytes 32-63) differentiates key types.

### TEK Sizing
| Key Type | Size | Structure | Rotation |
|----------|------|-----------|----------|
| ZYG (General Membership) | 256 bytes | 16 × 128-bit blocks | 6-month |
| ZYI1/ZYI2 (Interop) | 464 bytes | 29 × 128-bit blocks | 12-month |

## Components

### `cipher_keys.py`

Main integration module providing:

- **Intel HEX Parser** - Reads .hex key files with checksum verification
- **KeyTape Class** - Manages collections of keys with metadata
- **CipherEngine** - Session management and encryption operations
- **Auto-discovery** - Finds keys from directory structure
- **Time-based TEK Selection** - Calculates correct slot based on date/time
- **Secure Zeroization** - Proper key material cleanup

## Usage

```python
from cipher_keys import KeyTape, CipherEngine

# Load key tape from directory
tape = KeyTape.from_directory("/path/to/keys")

# Create cipher engine with current TEK
engine = CipherEngine(tape)

# Encrypt traffic
ciphertext = engine.encrypt(b"Hello MARS Network")

# Decrypt
plaintext = engine.decrypt(ciphertext)
```

## Integration Notes

### For phoenixnestmodem:
1. Replace placeholder XOR cipher with actual MIL-STD algorithm (likely AES-CTR)
2. Add Qt QVariant parser for .db metadata files
3. Integrate with ALE handshake for CKR exchange and nonce generation
4. Implement time synchronization for TEK slot coordination between stations

### Key File Locations
Keys are typically stored in Intel HEX format with naming convention:
- `ZYG_YYYYMM.hex` - General membership keys
- `ZYI1_YYYY.hex` - Interop keys type 1
- `ZYI2_YYYY.hex` - Interop keys type 2

## Security Considerations

- Key material is zeroized after use
- Keys should be stored on encrypted filesystems
- TEK rotation follows MARS schedule
- CKR (Cipher Key Register) exchange required for session establishment

## Status

✅ Intel HEX parsing with checksum verification  
✅ KeyTape management and auto-discovery  
✅ Time-based TEK slot calculation  
✅ Roundtrip encryption/decryption tested  
🚧 MIL-STD algorithm implementation (placeholder XOR currently)  
🚧 ALE/CKR integration  
