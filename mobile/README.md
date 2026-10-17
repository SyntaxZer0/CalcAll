# CalcAll on phones

The phone versions are the desktop app itself, compiled to WebAssembly so it
runs in a phone's browser. Nothing in `src/` is changed for them: everything
phone-specific lives in this folder.

| | |
|---|---|
| iPhone | open **https://syntaxzer0.github.io/CalcAll/** in Safari, tap Share, then **Add to Home Screen** |
| Android | download **CalcAll.apk** from the latest [release](https://github.com/SyntaxZer0/CalcAll/releases) and open it, or use the web page like on an iPhone (Chrome: ⋮ → Add to Home screen) |

Both work offline once they're on the phone. Definitions, history and
settings are kept in the browser's (or the app's) own storage.

## How it works

```
shim.h, shim.c   fit the desktop app to a phone, without changing it
web/             the page around it: shell.html, offline support, icons
android/         a small Android app that shows the web version full screen
Makefile         builds the web version into build/web
```

`src/gui.c` is compiled with `shim.h` included first. The shim swaps a few
raylib calls for its own:

- **Screen size.** The app lays itself out for a window about 480 pixels
  wide, as on a computer. The shim scales that up to the phone's real
  pixels, so text and lines stay sharp. Turned sideways, the layout gets
  wider instead, and the graph sits beside its list.
- **Touch.** Taps are clicks. Two fingers zoom the graph (pinch) and scroll
  lists (moving them together).
- **Typing.** The **⌨ Keyboard** button under the app brings up the
  phone's keyboard. What's typed is passed to the app in order. Phone
  keyboards have no arrow keys, so the strip has ◀ ▶ ▲ ▼ buttons for moving
  in and out of exponents, fractions and other boxes.
- **Files.** The home folder is kept in IndexedDB, and saved every couple of
  seconds and whenever the app is put away.
- **Frames.** The app's main loop hands control back to the browser after
  every frame (Emscripten's ASYNCIFY).

On a phone the app starts in KEYS mode, with the calculator buttons.

## Building it yourself

The GitHub workflow (`.github/workflows/mobile.yml`) does this on every
change, publishes the web version with GitHub Pages and attaches the
Android app to a release. To build the web version by hand you need
[Emscripten](https://emscripten.org) and raylib's source:

```sh
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git ../raylib
make -C ../raylib/src PLATFORM=PLATFORM_WEB
make -C mobile web RAYLIB=../../raylib
make -C mobile serve      # then open http://localhost:8000
```

The Android app needs the Android SDK: copy `build/web` to
`android/app/src/main/assets/www`, then run `gradle assembleRelease` in
`android/`.

## About the Android signing key

Android only installs an update over an app signed with the same key, so
`android/calcall.keystore` is kept in the repository: every build can then
replace the last one without losing your data. The flip side is that the key
is public, so anyone could sign an app that Android would accept as an
update to CalcAll. Only install CalcAll from this repository's releases. To
use a private key instead, keep it in a GitHub secret and point
`signingConfigs` in `android/app/build.gradle` at it.
