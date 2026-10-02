MAGIC MOUSE ON-DEMAND WAKE

This package is for the Lightning Magic Mouse 2 with product ID 0x0269.
It is intended for the macOS 27 problem where the mouse connects and shows its
battery level but does not move or click.

HOW TO USE

1. Keep these three files together in the same folder.
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
- No root daemon, login item, background process, or installation.
- No network access, downloads, telemetry, or automatic updates.
- No reading, recording, or storing keyboard or mouse input.
- No persistent logs or configuration files.

REMOVAL

Delete this folder. If you no longer need to run the workaround, you can also
disable Terminal under System Settings > Privacy & Security > Input Monitoring.

This is a temporary workaround, not an Apple-supported repair. Stop using it
after Apple ships a macOS update that restores normal Magic Mouse operation.
