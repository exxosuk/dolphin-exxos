#!/bin/bash
# Build computer.so against the system Qt5/KF5 dev packages.
# (MX 23 can install them; MX 21 could not -- see build.sh.staged-bullseye.)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
INC=/usr/include/x86_64-linux-gnu/qt5
g++ -std=c++17 -fPIC -shared -O2 -w \
    -DQT_NO_KEYWORDS \
    -I"$INC" -I"$INC/QtCore" -I"$INC/QtGui" -I"$INC/QtNetwork" -I/usr/lib/x86_64-linux-gnu/qt5/mkspecs/linux-g++ \
    -I/usr/include/KF5 -I/usr/include/KF5/KIOCore -I/usr/include/KF5/KCoreAddons \
    -I/usr/include/KF5/Solid -I/usr/include/KF5/KI18n \
    -o "$HERE/computer.so" "$HERE/computer.cpp" \
    -lKF5KIOCore -lKF5Solid -lKF5I18n -lKF5CoreAddons -lQt5Core
echo "Built: $HERE/computer.so"
nm -D --defined-only "$HERE/computer.so" | grep ' T kdemain' && echo "entry point OK"
