#!/bin/bash
# setup.sh - Quick setup script for openEMS project
# Creates the proper directory structure

PROJECT_NAME="openEMS"

echo "================================"
echo "openEMS Setup Script"
echo "================================"
echo ""

# Check if directory exists
if [ -d "$PROJECT_NAME" ]; then
    echo "⚠️  Directory '$PROJECT_NAME' already exists!"
    read -p "Do you want to continue? (y/n): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Setup cancelled."
        exit 1
    fi
fi

echo "Creating directory structure..."

# Create main directories
mkdir -p "$PROJECT_NAME/src/outputs"
mkdir -p "$PROJECT_NAME/src/displays"
mkdir -p "$PROJECT_NAME/src/inputs"
mkdir -p "$PROJECT_NAME/src/lib"
mkdir -p "$PROJECT_NAME/src/test"
mkdir -p "$PROJECT_NAME/docs/getting-started"
mkdir -p "$PROJECT_NAME/docs/guides/sensor-types"
mkdir -p "$PROJECT_NAME/docs/guides/configuration"
mkdir -p "$PROJECT_NAME/docs/reference"

echo "✅ Created: $PROJECT_NAME/"
echo "✅ Created: $PROJECT_NAME/src/"
echo "✅ Created: $PROJECT_NAME/src/outputs/"
echo "✅ Created: $PROJECT_NAME/src/displays/"
echo "✅ Created: $PROJECT_NAME/src/inputs/"
echo "✅ Created: $PROJECT_NAME/src/lib/"
echo "✅ Created: $PROJECT_NAME/src/test/"
echo "✅ Created: $PROJECT_NAME/docs/"
echo "✅ Created: $PROJECT_NAME/docs/getting-started/"
echo "✅ Created: $PROJECT_NAME/docs/guides/sensor-types/"
echo "✅ Created: $PROJECT_NAME/docs/guides/configuration/"
echo "✅ Created: $PROJECT_NAME/docs/reference/"

echo ""
echo "Directory structure created successfully!"
echo ""
echo "Next steps:"
echo "1. Copy the following files to their locations:"
echo ""
echo "   src/"
echo "   ├── main.cpp"
echo "   ├── config.h               ⚠️ EDIT THIS FILE"
echo "   ├── advanced_config.h"
echo "   ├── alarm.cpp"
echo "   ├── inputs/"
echo "   │   ├── input.h"
echo "   │   ├── input_manager.h"
echo "   │   ├── input_manager.cpp"
echo "   │   ├── sensor_read.cpp"
echo "   │   ├── serial_config.h"
echo "   │   └── serial_config.cpp"
echo "   ├── lib/"
echo "   │   ├── sensor_types.h"
echo "   │   ├── sensor_library.h"
echo "   │   ├── sensor_calibration_data.h"
echo "   │   ├── application_presets.h"
echo "   │   └── platform.h"
echo "   ├── outputs/"
echo "   │   ├── output_base.h"
echo "   │   ├── output_manager.cpp"
echo "   │   ├── output_can.cpp"
echo "   │   ├── output_realdash.cpp"
echo "   │   ├── output_serial.cpp"
echo "   │   └── output_sdlog.cpp"
echo "   ├── displays/"
echo "   │   └── display_lcd.cpp"
echo "   └── test/"
echo "       ├── test_mode.h"
echo "       ├── test_mode.cpp"
echo "       └── test_value_generator.cpp"
echo ""
echo "   Root directory:"
echo "   └── platformio.ini"
echo ""
echo "2. Edit src/config.h to match your hardware"
echo "3. (Optional) Edit src/advanced_config.h for advanced features"
echo "4. Run: cd $PROJECT_NAME && pio run"
echo ""
echo "For detailed instructions:"
echo "  - docs/README.md - Complete system guide"
echo "  - docs/getting-started/QUICK_REFERENCE.md - Quick lookup"
echo "  - docs/getting-started/DIRECTORY_SETUP.md - File organization"
