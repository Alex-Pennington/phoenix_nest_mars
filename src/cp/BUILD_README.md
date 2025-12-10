# Communications Processor - Standalone GPL Build

## Build Status: ✅ SUCCESS

Built on Ubuntu 24 with Qt 5.15.13

## What Was Changed

The original CP code referenced external CUI (Controlled Unclassified Information) components that weren't included in the GPL release:

1. **MSCCipherLibraryClass** - Encryption library (stubbed)
2. **ClassKeyLoader** - Key management UI (stubbed)
3. **ClassSignMessageGPL** - Fixed include path (local copy)

### Files Modified

| File | Change |
|------|--------|
| `cipherlibraryclass_global.h` | Changed DLL export to static |
| `dialogsettings.h` | Fixed keyloader include path |
| `dialogsettings.cpp` | Added cipherlibraryclass include |
| `dialogchat.h` | Fixed classsignmessage include path |
| `mainwindow.cpp` | Fixed cipherlibraryclass include path |

### Files Added

| File | Purpose |
|------|---------|
| `CP-standalone.pro` | New project file without external deps |
| `keyloader.h` | Stub key loader (shows "not available" message) |
| `cipherlibraryclass_stub.cpp` | Passthrough cipher (plaintext only) |

## Building

```bash
# Install dependencies (Ubuntu/Debian)
apt-get install qtbase5-dev qt5-qmake libqt5serialport5-dev libqt5texttospeech5-dev

# Build
qmake CP-standalone.pro
make -j4
```

## Limitations

This GPL build operates in **plaintext only** mode:

- ❌ No encryption/decryption
- ❌ No key management
- ✅ Full message routing
- ✅ V3PROTOCOL XML
- ✅ MS-DMT modem interface
- ✅ ALE controller integration
- ✅ TCP/UDP networking
- ✅ Message queuing with priority

## Integration with phoenixnestmodem

CP communicates with modems via the MS-DMT protocol on two TCP ports:

- **CMD Port** (default 5001): Control commands
- **DATA Port** (default 5000): Payload data

To integrate with phoenixnestmodem, implement an MS-DMT compatible server.

## License

GPL v3 - See gpl-3.0.txt
