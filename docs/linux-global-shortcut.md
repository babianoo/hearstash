# The capture shortcut on Linux

## Recommendation

Register the shortcuts through the XDG **GlobalShortcuts portal**
(`org.freedesktop.portal.GlobalShortcuts`), and keep the commands
(`hearstash --capture`, `--read-screen` and `--stash`) as the fallback
for desktops whose portal doesn't offer it. Not done yet; this note
records the decision for when it is.

## Today

Wayland lets no app grab keys, so on Linux the user binds a key in their
desktop to each of `hearstash --capture`, `hearstash --read-screen` and
`hearstash --stash`. Each command passes the press to the running app
through a socket in `$XDG_RUNTIME_DIR` (`src/ui/global_hotkey.cpp`). It works everywhere, but the user has to find
their desktop's shortcut settings and type a command with the right path.
On Windows the app registers the key itself (`RegisterHotKey`), so Linux is
the odd one out.

## The portal

The app asks the desktop for a shortcut over D-Bus and the desktop owns
the key:

1. `CreateSession`, then `BindShortcuts` with three shortcuts, `capture`,
   `read-screen` and `stash`, and preferred triggers such as `Meta+Alt+C`,
   `Meta+Alt+X` and `Meta+Alt+Z` (matching the Windows defaults).
2. The desktop shows a one-time dialog where the user accepts that key or
   picks another. No command to type.
3. Each press arrives as an `Activated` signal on the session.
4. The desktop remembers the binding across restarts; the user changes it
   in the desktop's own shortcut settings, where it's listed under
   Hearstash.

## Desktop support

| Desktop | Portal backend | GlobalShortcuts |
| --- | --- | --- |
| KDE Plasma 5.27+ | xdg-desktop-portal-kde | Yes |
| Hyprland | xdg-desktop-portal-hyprland | Yes |
| GNOME 48+ | xdg-desktop-portal-gnome | Yes |
| Sway, other wlroots | xdg-desktop-portal-wlr | No |

Sway and friends are why the socket stays: the app tries the portal
first, and only when it isn't there does Settings > Shortcuts show the
commands to bind, as it does now.

## Implementation sketch

- Lives in `src/ui/global_hotkey.cpp`; `GlobalHotkey`'s interface barely
  changes. `set_shortcut` stays a no-op on Linux, or reopens the portal's
  dialog.
- D-Bus through `sd-bus` (libsystemd) or `libdbus`. Hand the bus's fd to
  `Fl::add_fd`, as the socket listener already is, so signals arrive on
  the UI thread.
- The portal identifies an unsandboxed app by its `.desktop` file id; KDE
  names and saves the shortcut under it. So the app needs a
  `hearstash.desktop` installed (an install rule in `CMakeLists.txt`), or,
  on xdg-desktop-portal 1.19+, to call
  `org.freedesktop.host.portal.Registry.Register` with `hearstash` before
  opening the session. Without one, the binding may not stick.
- The window's app id is already `hearstash` (FLTK takes it from argv[0]
  in `main_win.show(argc, argv)`), which matches.
- Errors from the portal (no backend, user cancelled the dialog) fall back
  to the socket and the command in Settings, not to a failure.

## Rejected

- **Reading `/dev/input` (evdev).** Works anywhere, but needs the user in
  the `input` group and sees every key typed in every app: a keylogger.
- **KDE's KGlobalAccel over D-Bus.** KDE only, and the portal already
  covers KDE.
- **`XGrabKey`.** Only sees keys while an X11/XWayland window has focus,
  so it misses mpv or a game running natively on Wayland.
