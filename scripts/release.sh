#!/bin/bash
# Phoenix Nest MARS - Release Packaging Script
# Creates distributable tarballs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RELEASE_DIR="$PROJECT_ROOT/releases"

VERSION="${1:-dev}"
DATE=$(date +%Y%m%d)
RELEASE_NAME="phoenix-nest-mars-${VERSION}-${DATE}"

echo "========================================"
echo "Creating Release: $RELEASE_NAME"
echo "========================================"

mkdir -p "$RELEASE_DIR"

# Create source release (no binaries)
create_source_release() {
    echo "Creating source release..."
    
    TEMP_DIR=$(mktemp -d)
    mkdir -p "$TEMP_DIR/$RELEASE_NAME"
    
    # Copy source files
    cp -r "$PROJECT_ROOT/src" "$TEMP_DIR/$RELEASE_NAME/"
    cp -r "$PROJECT_ROOT/docs" "$TEMP_DIR/$RELEASE_NAME/"
    cp -r "$PROJECT_ROOT/scripts" "$TEMP_DIR/$RELEASE_NAME/"
    cp "$PROJECT_ROOT/README.md" "$TEMP_DIR/$RELEASE_NAME/"
    cp "$PROJECT_ROOT/.gitignore" "$TEMP_DIR/$RELEASE_NAME/"
    
    # Remove build artifacts
    find "$TEMP_DIR" -name "*.o" -delete
    find "$TEMP_DIR" -name "moc_*" -delete
    find "$TEMP_DIR" -name "ui_*" -delete
    find "$TEMP_DIR" -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
    find "$TEMP_DIR" -name "*.pyc" -delete
    find "$TEMP_DIR" -name "Makefile" -delete
    find "$TEMP_DIR" -name ".qmake.stash" -delete
    
    # Create tarball
    cd "$TEMP_DIR"
    tar -czvf "$RELEASE_DIR/${RELEASE_NAME}-src.tar.gz" "$RELEASE_NAME"
    
    rm -rf "$TEMP_DIR"
    echo "Created: $RELEASE_DIR/${RELEASE_NAME}-src.tar.gz"
}

# Create binary release (with built executables)
create_binary_release() {
    echo "Creating binary release..."
    
    # First build everything
    "$SCRIPT_DIR/build.sh" all
    
    TEMP_DIR=$(mktemp -d)
    mkdir -p "$TEMP_DIR/$RELEASE_NAME/bin"
    mkdir -p "$TEMP_DIR/$RELEASE_NAME/lib"
    
    # Copy binaries
    cp "$PROJECT_ROOT/src/cp/communicationsprocessor" "$TEMP_DIR/$RELEASE_NAME/bin/cp" 2>/dev/null || \
    cp "$PROJECT_ROOT/src/cp/CP" "$TEMP_DIR/$RELEASE_NAME/bin/cp" 2>/dev/null || \
    echo "Warning: CP binary not found"
    
    cp "$PROJECT_ROOT/src/smlinux/StationMapper" "$TEMP_DIR/$RELEASE_NAME/bin/" 2>/dev/null || \
    echo "Warning: StationMapper binary not found"
    
    # Copy Python modules
    cp -r "$PROJECT_ROOT/src/propagation" "$TEMP_DIR/$RELEASE_NAME/lib/"
    cp -r "$PROJECT_ROOT/src/crypto" "$TEMP_DIR/$RELEASE_NAME/lib/"
    
    # Copy docs
    cp -r "$PROJECT_ROOT/docs" "$TEMP_DIR/$RELEASE_NAME/"
    cp "$PROJECT_ROOT/README.md" "$TEMP_DIR/$RELEASE_NAME/"
    
    # Create run scripts
    cat > "$TEMP_DIR/$RELEASE_NAME/bin/run-cp.sh" << 'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/cp" "$@"
EOF
    chmod +x "$TEMP_DIR/$RELEASE_NAME/bin/run-cp.sh"
    
    cat > "$TEMP_DIR/$RELEASE_NAME/bin/run-stationmapper.sh" << 'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/StationMapper" "$@"
EOF
    chmod +x "$TEMP_DIR/$RELEASE_NAME/bin/run-stationmapper.sh"
    
    # Create tarball
    cd "$TEMP_DIR"
    tar -czvf "$RELEASE_DIR/${RELEASE_NAME}-linux-x86_64.tar.gz" "$RELEASE_NAME"
    
    rm -rf "$TEMP_DIR"
    echo "Created: $RELEASE_DIR/${RELEASE_NAME}-linux-x86_64.tar.gz"
}

case "${2:-source}" in
    source)
        create_source_release
        ;;
    binary)
        create_binary_release
        ;;
    all)
        create_source_release
        create_binary_release
        ;;
    *)
        echo "Usage: $0 <version> [source|binary|all]"
        echo ""
        echo "  version  - Version string (e.g., 0.1.0)"
        echo "  source   - Source code only (default)"
        echo "  binary   - Includes compiled binaries"
        echo "  all      - Both source and binary"
        exit 1
        ;;
esac

echo ""
echo "Release files in: $RELEASE_DIR/"
ls -la "$RELEASE_DIR/"
