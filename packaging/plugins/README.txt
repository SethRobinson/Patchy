Patchy plug-ins folder
======================

Put classic Photoshop filter plug-ins (.8bf files) in this folder, or in
subfolders of it. Both 32-bit and 64-bit plug-ins work.

After adding files, choose Plugins > Rescan Plug-in Folders in Patchy (the
folder is also scanned every time Patchy starts). Each plug-in then appears
under Plugins > Legacy Photoshop Plug-ins, grouped by the category it
declares, and runs on the active pixel layer inside the selection, with its
own settings dialog and preview.

Notes:
- Only filter plug-ins (.8bf) run. File-format (.8bi) and automation (.8li)
  plug-ins are listed as unsupported.
- Plug-ins are Windows programs. The macOS and Linux builds of Patchy cannot
  run them.
- Each plug-in runs in a separate helper program (patchy-8bf-host32.exe or
  patchy-8bf-host64.exe), so a crashing plug-in cannot take Patchy down. It
  still runs with your user permissions: only install plug-ins you trust.
- More folders can be added under File > Preferences > Plug-ins.

Details: https://github.com/SethRobinson/Patchy/blob/main/docs/plugins.md
