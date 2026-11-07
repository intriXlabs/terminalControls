# terminalControl

<div align="center">

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17%2B-orange.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20POSIX-lightgrey.svg)
![Dependencies](https://img.shields.io/badge/dependencies-0-brightgreen.svg)
![Single File](https://img.shields.io/badge/single--file-yes-success.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

**Input that doesn't just arrive — it's delivered.**

*A single-file C++17 terminal input library with frame-based polling,
keyboard + mouse decoding, and clean RAII lifecycle.*


</div>

---

## Important Note

terminalControl is designed for **POSIX terminals** (Linux, macOS, BSD, WSL) and requires **ANSI escape codes**. It uses `termios`, `select`, `ioctl`, and `poll` directly — no wrappers, no abstractions.

### The Single-File Version

The library is one `.cpp` file with **no external dependencies**. It relies only on POSIX system calls and the C++17 standard library.

Requirements:

* POSIX terminal (`termios`, `select`, `ioctl`)
* ANSI escape code support
* A real TTY on stdin (`enableRaw()` returns `false` otherwise)

Terminal resize detection is handled internally via `ioctl(TIOCGWINSZ)`. Mouse decoding uses **SGR extended mode** (1006), so modern terminals — kitty, alacritty, wezterm, GNOME Terminal, iTerm2, Windows Terminal (WSL) — all work out of the box.

**terminalControl handles input; what you do with it is up to you.**

---

## 📖 Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [The Two State Tiers](#-the-two-state-tiers)
- [Architecture](#-architecture)
- [Quick Start](#-quick-start)
- [Use Cases](#-use-cases)
- [Why terminalControl](#-why-terminalcontrol)
- [Comparison](#-comparison)
- [Design Philosophy](#-design-philosophy)
- [Parsing Internals](#-parsing-internals)
- [License](#-license)

---

## 🌟 Overview

terminalControl is a lightweight C++ library for reading raw terminal input. It puts the terminal into raw mode, enables mouse tracking, and delivers a clean per-frame snapshot of everything that happened — keys pressed, mouse moved, buttons clicked, scroll wheel turned, terminal resized.

The name is literal: it's a class that **controls the terminal**. No game loop, no renderer, no scene graph — just input, decoded and handed to you.

Every `update()` call is a frame. Within that frame, all state is refreshed, all one-shot flags are reset, and every byte read from stdin is parsed. You read the result. You clear nothing. Next frame, same thing.

### Why Frame-Based Input?

Terminal input is byte soup. Escape sequences arrive split across `read()` calls. `ESC` alone is ambiguous with the start of every arrow key. Mouse reports are five to twelve bytes of structured text. Modifier state is encoded as a bitmask inside the button byte.

Decoding all that by hand, correctly, is a weekend of work — and you'll still get partial-sequence bugs.

terminalControl does it once. You get structs.

- **Zero Dependencies** — No libraries. No frameworks. No linker headaches.
- **Frame-Based** — One call per frame, one clean snapshot out.
- **Keyboard + Mouse** — Both, in the same poll cycle, in the same frame.
- **RAII Cleanup** — Destructor restores the terminal. Every time.

---

## ✨ Features

### ⌨️ Keyboard

- **Letters & Digits** — `a`–`z`, `n0`–`n9`
- **Special Keys** — Enter, Tab, Backspace, Escape, Space
- **Navigation** — Arrow keys, Home, End, PageUp, PageDown, Del, Insert
- **Function Keys** — F1 through F12
- **Modifiers** — Ctrl, Alt, Shift (decoded from both CSI and control bytes)
- **Raw Byte Access** — `lastChar` for punctuation and non-letter printable input

### 🖱️ Mouse

- **Click Detection** — Left, Middle, Right
- **Held State** — `mouse.left` stays `true` while the button is down
- **Edge Events** — `pressed`, `released` fire for exactly one frame
- **Motion Tracking** — `mouse.motion` per frame, `x`/`y` always current
- **Scroll Wheel** — Up and Down, one-shot per frame
- **Modifiers** — Shift, Alt, Ctrl held during mouse events are folded into `keys`

### 🧩 Developer-Friendly

- **One Class** — `terminalControl`, non-copyable, one instance per process
- **Per-Frame Model** — `update(ms)` polls, parses, returns. Nothing else.
- **Safe by Default** — Partial sequences buffered across reads, never dropped
- **RAII Cleanup** — Terminal restored on destruction, even on early return
- **Resize Detection** — `t.resized` fires for one frame, `t.rows`/`t.cols` always current

---

## 🎭 The Two State Tiers

Fadescii had two modes. terminalControl has two **state categories**. Every field is one or the other.

### ▸ One-Shot — *The Edge*

True for exactly one frame. `t.keys.*`, `t.mouse.pressed`, `t.mouse.released`, `t.mouse.motion`, `t.mouse.scrollUp`, `t.mouse.scrollDown`, `t.resized`.

**Feels like:** a discrete event. A keypress. A click. A scroll tick.

```cpp
if (t.keys.space) jump();
if (t.mouse.pressed) click();
```

### ▸ Sticky — *The Hold*

True until the physical thing changes. `t.mouse.left`, `t.mouse.middle`, `t.mouse.right`, `t.mouse.x`, `t.mouse.y`, `t.rows`, `t.cols`, `t.quit`.

**Feels like:** a physical state you query. Is the button down? Where is the cursor? How big is the window?

```cpp
if (t.mouse.left) drag();
if (t.quit) break;
```

You never clear anything. The library does it for you.

---

## 🏗 Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Application Layer                      │
│   ┌──────────────┐  ┌────────────────┐  ┌───────────────┐  │
│   │  update(ms)  │  │  keys.* /      │  │  write() /    │  │
│   │              │  │  mouse.*       │  │  moveTo()     │  │
│   └──────────────┘  └────────────────┘  └───────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                     Frame Layer                             │
│   ┌────────────────────────────────────────────────────┐   │
│   │   Reset One-Shot  •  Poll stdin  •  Refresh size   │   │
│   └────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                     Parser Layer                            │
│   ┌────────────────────────────────────────────────────┐   │
│   │   Buffer  •  CSI / SS3 / SGR  •  Modifier Bitmask  │   │
│   └────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                     Terminal Layer                          │
│   ┌────────────────────────────────────────────────────┐   │
│   │   termios  •  select  •  ioctl  •  writeAll        │   │
│   └────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            ↓
                    stdin / stdout
```

terminalControl reads directly from stdin and writes directly to stdout. There is no intermediate event queue, no callback dispatch, no render pass — just bytes in, structs out, bytes back when you call `write()`.

---

## 🚀 Quick Start

### Prerequisites

- **C++17 or higher** compiler
- **POSIX terminal** — Linux, macOS, BSD, WSL
- **ANSI escape code support** — universal on modern terminals

### Installation

```bash
# Clone the repository
git clone https://github.com/yourname/terminalControl.git

# Compile with optimizations
g++ -std=c++17 -O2 demo.cpp -o demo

# Run the demo
./demo
```

### Minimal Example

```cpp
#include "terminal.cpp"

int main() {
    terminalControl t;

    // Enter raw mode. false = stdin isn't a TTY.
    if (!t.enableRaw()) return 1;

    // Enable mouse tracking + alt screen + hide cursor.
    t.enableMouse();
    t.clear();

    // Frame loop
    while (!t.quit) {
        t.update(16);                 // poll ~60 FPS

        if (t.keys.escape)   t.write("ESC\r\n");
        if (t.keys.enter)    t.write("ENTER\r\n");
        if (t.keys.up)       t.write("UP\r\n");
        if (t.keys.down)     t.write("DOWN\r\n");

        if (t.mouse.left)    t.write("LEFT HELD\r\n");
        if (t.mouse.pressed) t.write("CLICK\r\n");
        if (t.mouse.motion)  t.write("MOVE\r\n");
    }

    return 0;   // destructor restores terminal
}
```

---

## 🎯 Use Cases

### 1. Terminal Games
Roguelikes, arcade games, anything that needs arrow keys and mouse clicks at 60 FPS. Frame-based model maps directly onto a game loop.

```cpp
while (!t.quit) {
    t.update(16);
    if (t.keys.left)  player.x--;
    if (t.keys.right) player.x++;
    if (t.mouse.pressed) shoot(t.mouse.x, t.mouse.y);
    render();
}
```

### 2. Full-Screen TUIs
File managers, log viewers, dashboards — anywhere a plain `ncurses` import feels heavy. terminalControl gives you the input layer, you build the renderer.

### 3. Interactive CLI Tools
Setup wizards, installers, and prompts that respond to arrow keys and mouse clicks instead of `y/n` text.

### 4. Terminal Splash Screens
Combine with an animation library — poll input during the animation to let users skip it.

### 5. Live Demos & Streams
Anything where you need to *show* an interactive terminal on screen. The alt-screen cleanup means OBS captures a clean exit.

---

## 🌈 Why terminalControl

Most terminal libraries fall into one of two camps: they build a whole TUI framework (ncurses, FTXUI, notcurses), or they expose raw `termios` and let you decode escape sequences yourself.

terminalControl sits in between. It doesn't want to render your interface. It doesn't want to own your terminal. It just wants to turn stdin into structs — and then get out of the way.

If you're writing a game or a TUI and you're about to hand-decode `ESC [ 1 ; 5 A`, this is what it's for.

---

## ⚖️ Comparison

| | **terminalControl** | Raw `termios` | ncurses | FTXUI |
|---|---|---|---|---|
| **Dependencies** | None | None | ncurses | Several |
| **Setup** | One class | Many syscalls | `initscr` + config | Framework to learn |
| **Keyboard** | Full decode | Manual | Full decode | Full decode |
| **Mouse** | Full SGR decode | Manual | Config + decode | Config + decode |
| **Frame model** | Built-in | Manual | Manual | Event loop |
| **Scope** | Input only | Input only | Full TUI | Full TUI |
| **Cleanup** | RAII | Manual | `endwin` | RAII |
| **Learning curve** | Minutes | Hours | Days | Days |

terminalControl isn't trying to replace your TUI library. It's trying to be the input layer *under* it.

---

## 🧬 Design Philosophy

terminalControl is small on purpose.

- It doesn't try to be a rendering engine.
- It doesn't try to be a framework.
- It doesn't ask you to adopt a build system, a config file, or a new way of thinking.

It's one class that does one thing well: **turn raw terminal input into a clean, per-frame snapshot**.

The parsing is buffered, not event-driven. `ESC` alone is resolved with a one-frame grace period, not a timeout hack. Mouse uses SGR extended mode (1006), not the legacy X10 encoding, because every terminal that supports mouse in 2024 supports SGR.

The state model has exactly two categories. One-shot and sticky. That's the whole mental model.

---

## 🔬 Parsing Internals

The parser is the part most likely to bite you when debugging, so here's the exact order of operations.

### Entry point

`pollInput(ms)` resets per-frame flags, resolves a leftover lone-`ESC` from the previous frame, then `select()`s on stdin. On readable, reads up to 8192 bytes and calls `feed(byte)` for each. `feed()` appends to `buf_` and calls `parseBuffer()`. `parseBuffer()` loops until `buf_` is empty or the front bytes can't yet form a complete sequence.

### Resolution order per buffer state

Starting from `buf_[0]`:

**1. `ESC`-prefixed sequences** (`first == 0x1b`)

- `buf_.size() == 1` → **wait**. Could be a lone `ESC` or the start of a sequence.
- `buf_[1] == '['` → **CSI**. If `buf_[2] == '<'`, it's an SGR mouse report. Otherwise, scan for final byte (`0x40`–`0x7E`) and dispatch.
- `buf_[1] == 'O'` → **SS3**. Needs 3 bytes. Dispatch `buf_[2]`.
- Otherwise → **Alt + printable**. `keys.alt = true`, then `onPrintable()`.

**2. Single-byte control codes**

| Byte | Meaning |
|---|---|
| `0x03` | Ctrl+C — `keys.ctrl`, `keys.c`, `t.quit = true` |
| `\r`, `\n` | `enter` |
| `\t` | `tab` |
| `0x7f`, `0x08` | `backspace` |
| `0x01`–`0x1a` | Ctrl+letter |

**3. Printable ASCII** (`0x20`–`0x7e`) → `onPrintable()`. Sets `lastChar`, `space`, and the letter/digit flag.

**4. UTF-8 lead bytes** — pattern-matched against `0xC0`/`0xE0`/`0xF0` masks. Full codepoint consumed in one shot, not decoded.

**5. Anything else** — dropped.

### The lone-`ESC` trick

```cpp
if (buf_.size() == 1 && (unsigned char)buf_[0] == 0x1b) {
    keys.escape = true;
    buf_.clear();
}
```

If exactly one `ESC` byte is sitting in the buffer at frame start, it's a real `ESC` keypress. Standard fix for the "ESC vs. escape sequence" ambiguity. Costs one frame of latency on the `ESC` key.

### Modifier decoding

For CSI sequences of the form `ESC [ <num> ; <mod> <final>`, `<mod>` is a 1-based bitmask:

```
mod - 1:  bit 0 = Shift, bit 1 = Alt, bit 2 = Ctrl
```

So `ESC[1;5A` is Ctrl+Up, `ESC[1;2C` is Shift+Right, `ESC[1;7D` is Ctrl+Alt+Left.

### Mouse

SGR extended mode (1006). Button byte `b`:

- `b & 4` → Shift, `b & 8` → Alt, `b & 16` → Ctrl. Folded into `keys`.
- `b & 32` → motion event. Sets `mouse.motion`, returns early.
- `b & 64` → scroll. `btn == 0` is up.
- Otherwise `btn = b & 3`: `0` left, `1` middle, `2` right.

Coordinates arrive 1-based, stored 0-based.

---

## things to be noticed

Ctrl+C in raw mode doesn't send a signal — terminalControl intercepts `0x03` and sets `t.quit = true` explicitly. This is the only way to catch it when `ISIG` is disabled.

Mouse tracking enables **four** modes at once: `?1000h` (clicks), `?1002h` (drags), `?1003h` (all motion), `?1006h` (SGR encoding). Disabling at cleanup turns off `?1000` and `?1006` only, which is sufficient — the `?1002`/`?1003` modes are implicitly reset when `?1000` goes off.

If your terminal gets stuck in raw mode after a crash (SIGSEGV, SIGKILL — destructors don't run), run `stty sane` to restore it.

![feature](gifs/feature.png)

---

## 📄 License

Licensed under the apachee 2.0 License. See [LICENSE](LICENSE) for details.

---

<div align="center">

**Built for the terminal.**

*"A key is pressed. A click is caught. The frame delivers."*

[⬆ Back to Top](#terminalcontrol)

</div>
