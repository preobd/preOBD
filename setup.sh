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
mkdir -p "$PROJECT_NAME/docs"

echo "✅ Created: $PROJECT_NAME/"
echo "✅ Created: $PROJECT_NAME/src/"
echo "✅ Created: $PROJECT_NAME/src/outputs/"
echo "✅ Created: $PROJECT_NAME/src/displays/"
echo "✅ Created: $PROJECT_NAME/docs/"

echo ""
echo "Directory structure created successfully!"
echo ""
echo "Next steps:"
echo "1. Copy the following files to their locations:"
echo ""
echo "   src/"
echo "   ├── main.cpp"
echo "   ├── config.h               ⚠️ EDIT THIS FILE"
echo "   ├── sensor_types.h"
echo "   ├── sensors.cpp"
echo "   ├── sensor_read.cpp"
echo "   ├── alarm.cpp"
echo "   ├── outputs/"
echo "   │   ├── output_base.h"
echo "   │   ├── output_manager.cpp"
echo "   │   ├── output_can.cpp"
echo "   │   ├── output_realdash.cpp"
echo "   │   ├── output_serial.cpp"
echo "   │   └── output_sdlog.cpp"
echo "   └── displays/"
echo "       └── display_lcd.cpp"
echo ""
echo "   Root directory:"
echo "   └── platformio.ini"
echo ""
echo "2. Edit src/config.h to match your hardware"
echo "3. Run: cd $PROJECT_NAME && pio run"
echo ""
echo "For detailed instructions, see DIRECTORY_SETUP.md"
