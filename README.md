MAGIC MOUSE ON-DEMAND WAKE

This package is for the Lightning Magic Mouse 2 with product ID 0x0269.
It is intended for the macOS 27 problem where the mouse connects and shows its
battery level but does not move or click.

AUTOMATIC MODE (RECOMMENDED)

The one-shot fix does not survive a screen lock, a reconnect, or a restart.
To have the mouse woken automatically every time:

1. Double-click "Install Auto Wake.command".
2. System Settings opens at Privacy & Security > Input Monitoring.
   Enable "Magic Mouse Keeper". This is the only manual step.
3. Within about 15 seconds the helper starts working. From then on it wakes
   the mouse by itself a couple of seconds after it reconnects.

What the installer adds, all inside your home folder and without sudo:

- ~/Applications/Magic Mouse Keeper.app   the same helper, built from
                                          magicmouse-wake.c and run with --watch
- ~/Library/LaunchAgents/io.github.magic-mouse-2-fix.keeper.plist
                                          starts the helper at login and keeps it running
- ~/Library/Logs/MagicMouseKeeper.log     state changes only (connected, woken, errors)

The helper checks every two seconds whether the mouse is connected without
Apple's native driver and only then opens the mouse to send the report.

Notes:

- It runs after you log in. At the login window after a restart the mouse is
  still not usable; it starts working a few seconds after login.
- Running the installer again rebuilds the helper, so macOS asks for the
  Input Monitoring permission again.
- To remove everything, double-click "Uninstall Auto Wake.command".

ONE-SHOT MODE

1. Keep the launcher and magicmouse-wake.c together in the same folder.
2. Make sure the Magic Mouse is switched on and connected through Bluetooth.
3. Double-click "Wake Magic Mouse.command".
4. If Apple Command Line Tools are missing, the launcher will offer Apple's
   installer. Complete that installation, then run the launcher again.
5. If macOS reports that HID access was denied, open:

   System Settings > Privacy & Security > Input Monitoring

   Enable Terminal, quit Terminal completely, and double-click the launcher again.
6. A successful run prints:

   Success: the native Magic Mouse driver is active.

WHAT IT DOES

- Compiles the readable C source into a temporary folder.
- Targets only Apple Magic Mouse 2 product 0x0269 and its mouse-pointer interface.
- Sends report ID F1 with payload 06 01 37.
- Verifies that Apple's native multitouch driver appeared.
- Deletes the temporary helper and exits.

WHAT IT DOES NOT DO

- No sudo or administrator password.
- No root daemon. One-shot mode installs nothing and leaves no background
  process; automatic mode installs only the per-user items listed above.
- No network access, downloads, telemetry, or automatic updates.
- No reading, recording, or storing keyboard or mouse input.
- No persistent logs or configuration files in one-shot mode.

REMOVAL

If you installed automatic mode, run "Uninstall Auto Wake.command" first.
Delete this folder. If you no longer need to run the workaround, you can also
disable Terminal under System Settings > Privacy & Security > Input Monitoring.

This is a temporary workaround, not an Apple-supported repair. Stop using it
after Apple ships a macOS update that restores normal Magic Mouse operation.

CREDITS

The wake-up technique, the original helper (magicmouse-wake.c) and the
"Wake Magic Mouse.command" launcher were created by Khaled Hawari:
https://github.com/khaledhawari/magic-mouse-2-fix-os27

The automatic mode (--watch, installer and uninstaller) and the build fixes
for mismatched Xcode / Command Line Tools setups were added by Renato Asterio
on top of that work. Released under the same MIT license; see LICENSE.
