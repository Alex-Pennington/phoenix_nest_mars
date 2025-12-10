#!/bin/bash
# Phoenix Nest MARS - Build Script
# Builds CP, SMLinux, and sets up Python components

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$PROJECT_ROOT/src"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "Phoenix Nest MARS Build System"
echo "========================================"
echo ""

# Check for Qt
check_qt() {
    if ! command -v qmake &> /dev/null; then
        echo -e "${RED}ERROR: qmake not found${NC}"
        echo "Install Qt5 development packages:"
        echo "  Ubuntu/Debian: sudo apt install qtbase5-dev qtcharts5-dev"
        exit 1
    fi
    echo -e "${GREEN}✓ Qt found: $(qmake --version | head -1)${NC}"
}

# Build Communications Processor
build_cp() {
    echo ""
    echo -e "${YELLOW}Building Communications Processor...${NC}"
    cd "$SRC_DIR/cp"
    
    if [ -f "CP-standalone.pro" ]; then
        qmake CP-standalone.pro
        make -j$(nproc)
        if [ -f "CP" ] || [ -f "CommunicationsProcessor" ]; then
            echo -e "${GREEN}✓ CP built successfully${NC}"
        else
            echo -e "${RED}✗ CP build failed${NC}"
            return 1
        fi
    else
        echo -e "${RED}✗ CP-standalone.pro not found${NC}"
        return 1
    fi
}

# Build Station Mapper Linux
build_smlinux() {
    echo ""
    echo -e "${YELLOW}Building Station Mapper Linux...${NC}"
    cd "$SRC_DIR/smlinux"
    
    if [ -f "StationMapper-linux.pro" ]; then
        qmake StationMapper-linux.pro
        make -j$(nproc)
        if [ -f "StationMapper" ]; then
            echo -e "${GREEN}✓ SMLinux built successfully${NC}"
        else
            echo -e "${RED}✗ SMLinux build failed${NC}"
            return 1
        fi
    else
        echo -e "${RED}✗ StationMapper-linux.pro not found${NC}"
        return 1
    fi
}

# Setup Python propagation module
setup_propagation() {
    echo ""
    echo -e "${YELLOW}Setting up Propagation module...${NC}"
    cd "$SRC_DIR/propagation"
    
    # Check Python dependencies
    python3 -c "import numpy, scipy, matplotlib" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Python dependencies available${NC}"
    else
        echo -e "${YELLOW}Installing Python dependencies...${NC}"
        pip3 install numpy scipy matplotlib --user
    fi
    
    # Check for voacapl
    if command -v voacapl &> /dev/null; then
        echo -e "${GREEN}✓ voacapl available - real predictions enabled${NC}"
    else
        echo -e "${YELLOW}⚠ voacapl not found - using simulated predictions${NC}"
        echo "  Install voacapl for real propagation predictions"
    fi
}

# Clean build artifacts
clean() {
    echo ""
    echo -e "${YELLOW}Cleaning build artifacts...${NC}"
    
    cd "$SRC_DIR/cp"
    make clean 2>/dev/null || true
    rm -f CP CommunicationsProcessor Makefile .qmake.stash
    rm -f moc_* ui_* qrc_* *.o
    
    cd "$SRC_DIR/smlinux"
    make clean 2>/dev/null || true
    rm -f StationMapper Makefile .qmake.stash
    rm -f moc_* ui_* qrc_* *.o
    
    echo -e "${GREEN}✓ Clean complete${NC}"
}

# Main
case "${1:-all}" in
    all)
        check_qt
        build_cp
        build_smlinux
        setup_propagation
        echo ""
        echo -e "${GREEN}========================================"
        echo "Build Complete!"
        echo "========================================${NC}"
        echo ""
        echo "Binaries:"
        echo "  CP:       $SRC_DIR/cp/CP"
        echo "  SMLinux:  $SRC_DIR/smlinux/StationMapper"
        ;;
    cp)
        check_qt
        build_cp
        ;;
    smlinux)
        check_qt
        build_smlinux
        ;;
    propagation)
        setup_propagation
        ;;
    clean)
        clean
        ;;
    *)
        echo "Usage: $0 [all|cp|smlinux|propagation|clean]"
        echo ""
        echo "  all         - Build everything (default)"
        echo "  cp          - Build Communications Processor only"
        echo "  smlinux     - Build Station Mapper Linux only"
        echo "  propagation - Setup Python propagation module"
        echo "  clean       - Remove build artifacts"
        exit 1
        ;;
esac
