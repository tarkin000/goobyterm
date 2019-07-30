**goobyterm**
  
goobyterm is basically a GtkStack with two children: a WebKit2GTK _WebView_ and VTE _Terminal_

I've found that beside the gaping security hole, having some server-side pty/websocket bridge available
is not very satisfying.

Last I checked, Google's own chrome(ium) terminal 'app' cannot be tabbed; don't get me started on trying
to get a native window area from Firefox, and Edge is right out- even with the recent switch to Blink.
Solutions like GateOne are way too hamfisted; solutions like ttyd and gotty are difficult for someone
who isn't ready to dive into TSX/JSX, typescript, yarn, gulp, or anything NodeJS-related.

So, after a few days of beating my head against the Gtk API, I present to you: a browser with a terminal widget.

**Hotkeys**:
- ALT + 0 - 9: switch to WebViews 0 - 9; 0 is always available, always a webview, and cannot be closed
- ALT + j    : triggers popup to select a WebView if you have more than 9; will still navigate to 0-9
- ALT + l    : set focus to the URL bar
- ALT + n    : create a new WebView, show it, and set focus to the URL bar
- ALT + t:   : select the VTE Terminal (running tmux)
- ALT + x    : close the current WebView, IFF a WebView is currently visible and it's not the initial WV

Builds on almost any recent-ish distro that has pkg-config, GTK3, WebKit2Gtk, and VTE 2.91  available; I shudder to think what it
would take to compile on Windows or OS-X.

**Dependencies** (Debian package names; others should be similar)
- pkg-config (for _mk.sh_
- libgtk-3-dev
- libwebkit2gtk-4.0-dev
- libvte-2.91-dev

_mk.sh_:
Just a convenience script to call g++ and pkg-config with necessary flags
**NB** comment out the 'DEBUG=-g' line to build goobyterm without debugging symbols
```shell
./mk.sh <source file> <output executable name>
```
Should work 99% of the time
**OR**
```shell
./mk.sh old <source file> <output executable name>
```
For VTE versions having vte_terminal_spawn _sync_ vs _async_

Enjoy!
