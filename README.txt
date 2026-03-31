# AutoClicker

A lightweight Windows autoclicker with a graphical interface, written in pure C using the WinAPI.

## Features

- **Mouse mode** — left, right, or middle button, single or double click
- **Keyboard mode** — automatically press any key
- **Customizable interval** — set delay in seconds and milliseconds
- **Cursor freeze** — lock clicks to a fixed position on screen
- **Global hotkey F3** — start/stop from any window, no need to switch focus
- **Click counter** — tracks how many times the clicker has fired

## Build

**MinGW:**
```bash
i686-w64-mingw32-gcc autoclicker.c -o autoclicker.exe \
    -luser32 -lgdi32 -mwindows -O2 -municode


MSVC (Developer Command Prompt):

cl autoclicker.c user32.lib gdi32.lib /link /subsystem:windows

Requirements
Windows 7 or later

MinGW or MSVC to compile
Usage
Choose mode: Mouse or Keyboard
Configure the button/key and click type
Set the interval
Press Start [F3] or use the F3 hotkey from any window
