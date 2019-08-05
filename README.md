**goobyterm**
  
Basically a GtkStack with two children: a WebKit2GTK _WebView_ and VTE _Terminal_

I've found that beside the gaping security hole, having some server-side pty/websocket bridge available
is not very satisfying.

Last I checked, Google's own chrome(ium) terminal 'app' cannot be tabbed; don't get me started on trying
to get a native window area from Firefox, and Edge is right out- even with the recent switch to Blink.
Solutions like GateOne are way too hamfisted; solutions like ttyd and gotty are difficult to modify for someone
who isn't ready to dive into TSX/JSX, typescript, yarn, gulp, or anything NodeJS-related.

So, after a few days of beating my head against the Gtk API, I present to you: a browser with a terminal widget.

**Hotkeys**:
- ALT + 0..9 : switch to WebViews 0 - 9; 0 is the 'root' webview, always available, and cannot be closed
- ALT + e    : show the 'dev log' in the main view area (see **dev log** below)
- ALT + j    : triggers popup to select a WebView if you have more than 9; will still navigate to 0-9
- ALT + l    : show a dialog with the contents of the 'dev log' (see **dev log** below)
- ALT + n    : create a new WebView, show it, and set focus to the URL bar
- ALT + t    : switch to VTE Terminal (running tmux)
- ALT + w    : add/remove domains to the TLS whitelist
- ALT + u    : set focus to the URL bar
- ALT + x    : close the current WebView, IFF a WebView is currently visible and it's not the initial WV

**dev log**:
 - As an alternative to `console.log()`, a custom function has been bound to the WebKit Javascript global environment.<br/>
Actually, it's bound to `window.external(_s_)`, where the only argument _s_ is converted to a string.<br/>
These strings are added to VTE Terminal instance managed by the application.<br/>
The contents of the 'dev log' can be copied and cleared by buttons on the dialog.

 - The last string passed is shown in the toolbar area of the application; all lines are displayed in a scrollable window<br/>
dialog via the 'dev log' hot key (see **Hotkeys** above).

 - The contents of the dev log can be cleared via a toolbar button on the dev log dialog

Builds on almost any recent-ish distro that has:
 - pkg-config
 - GTK3
 - WebKit2Gtk
 - VTE 2.91

I shudder to think what it would take to compile on Windows or OS-X.

`config.h` contains some macros that initialize certain internal data, like:
 - app default width & height
 - initial TLS domains whitelist
 - favicon display size

**Dependencies** (Debian package names; others should be similar)
- pkg-config (for _mk.sh_)
- libgtk-3-dev
- libwebkit2gtk-4.0-dev
- libvte-2.91-dev

_mk.sh_:
Just a convenience script to call g++ and pkg-config with necessary flags<br/>
**NB** comment out the 'DEBUG=-g' line to build goobyterm without debugging symbols
```shell
./mk.sh <source file> <output executable name>
```
Should work 99% of the time
**OR**
```shell
./mk.sh sync <source file> <output executable name>
```
For VTE versions having vte_terminal_spawn _sync_ vs _async_

Enjoy!

Changelog:
 < 02-AUG-2019: initial workup
   02-AUG-2019: added annunciator, changed URL entry hotkey
   03-AUG-2019: updated config.h & annunciator; added 'dev-log'
   03-AUG-2019: improved 'dev log'
   05-AUG-2019: improved 'dev log'; factored out about:blank; updated README.md & Makefile
