// terminal.cpp — g++ -std=c++17 -O2 terminal.cpp -o term
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <poll.h>

class terminalControl {
public:
    // ================= PUBLIC STATE =================
    struct KeyState {
        // letters
        bool a=false,b=false,c=false,d=false,e=false,f=false,g=false,h=false,
             i=false,j=false,k=false,l=false,m=false,n=false,o=false,p=false,
             q=false,r=false,s=false,t=false,u=false,v=false,w=false,x=false,
             y=false,z=false;
        // digits
        bool n0=false,n1=false,n2=false,n3=false,n4=false,n5=false,n6=false,
             n7=false,n8=false,n9=false;
        // special
        bool enter=false,tab=false,backspace=false,escape=false,space=false;
        bool up=false,down=false,left=false,right=false;
        bool home=false,end=false,pageUp=false,pageDown=false,del=false,insert=false;
        bool f1=false,f2=false,f3=false,f4=false,f5=false,f6=false,f7=false,
             f8=false,f9=false,f10=false,f11=false,f12=false;
        // modifiers (edge, this frame only)
        bool ctrl=false,alt=false,shift=false;
        // raw last printable char (0 = none)
        char lastChar = 0;
    };

    struct MouseState {
        // held state — true while button is down
        bool left=false, middle=false, right=false;
        // edge events — true for exactly one frame
        bool pressed=false, released=false;
        // motion / scroll — one frame
        bool motion=false, scrollUp=false, scrollDown=false;
        int x=0, y=0;
    };

    KeyState   keys;
    MouseState mouse;

    // live mouse coordinates (updated every frame, regardless of click)
    int mouseRow = 0;
    int mouseCol = 0;

    // one-shot global signals
    bool quit    = false;   // Ctrl+C
    bool resized = false;   // size changed this frame
    int  rows    = 0;
    int  cols    = 0;

    // ================= SIMPLE PUBLIC API =================
    bool enableRaw() {
        if (!isatty(STDIN_FILENO)) return false;
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return false;
        savedValid_ = true;
        termios r = saved_;
        r.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        r.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        r.c_cflag |= CS8;
        r.c_oflag &= ~OPOST;
        r.c_cc[VMIN]  = 0;
        r.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &r) != 0) return false;
        raw_ = true;
        refreshSize(true);
        return true;
    }

    void enableMouse() {
        if (!mouseTrack_) {
            w("\x1b[?1000h\x1b[?1006h\x1b[?1002h\x1b[?1003h");
            mouseTrack_ = true;
        }
        altScreen();
        hideCursor();
    }

    void update(int timeoutMs = 16) {
        // ensure raw mode still active
        if (!raw_ && savedValid_) {
            termios r = saved_;
            r.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
            r.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
            r.c_cflag |= CS8;
            r.c_oflag &= ~OPOST;
            r.c_cc[VMIN]  = 0;
            r.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &r) == 0) raw_ = true;
        }
        pollInput(timeoutMs);

        // sync live mouse coords every frame (even without motion event)
        mouseRow = mouse.y;
        mouseCol = mouse.x;
    }

    void disableRaw() {
        if (mouseTrack_) {
            w("\x1b[?1002l\x1b[?1006l\x1b[?1000l");
            mouseTrack_ = false;
        }
        showCursor();
        noAltScreen();
        if (raw_ && savedValid_) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
            raw_ = false;
        }
    }

    // ================= OUTPUT HELPERS =================
    void write(const std::string& s) { w(s); }
    void write(const char* s)        { w(std::string(s)); }
    void moveTo(int row, int col) {
        char b[32];
        int n = snprintf(b, sizeof b, "\x1b[%d;%dH", row, col);
        writeAll(b, (size_t)n);
    }
    void clear() { w("\x1b[2J\x1b[H"); }

    // ================= LIFECYCLE =================
    terminalControl() = default;
    ~terminalControl() { disableRaw(); }
    terminalControl(const terminalControl&) = delete;
    terminalControl& operator=(const terminalControl&) = delete;

private:
    termios saved_{};
    bool savedValid_   = false;
    bool raw_          = false;
    bool alt_          = false;
    bool mouseTrack_   = false;
    bool cursorHidden_ = false;
    int  lastRows_     = 0;
    int  lastCols_     = 0;
    std::string buf_;

    // ---------- low-level write ----------
    static void writeAll(const char* p, size_t n) {
        while (n > 0) {
            ssize_t w = ::write(STDOUT_FILENO, p, n);
            if (w < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    pollfd pfd{STDOUT_FILENO, POLLOUT, 0};
                    if (::poll(&pfd, 1, -1) < 0 && errno == EINTR) continue;
                    continue;
                }
                return;
            }
            if (w == 0) return;
            p += w;
            n -= (size_t)w;
        }
    }
    static void w(const std::string& s) { writeAll(s.data(), s.size()); }

    // ---------- internal screen helpers ----------
    void altScreen()   { if (!alt_) { w("\x1b[?1049h"); alt_ = true; } }
    void noAltScreen() { if (alt_)  { w("\x1b[?1049l"); alt_ = false; } }
    void hideCursor()  { if (!cursorHidden_) { w("\x1b[?25l"); cursorHidden_ = true; } }
    void showCursor()  { if (cursorHidden_)  { w("\x1b[?25h"); cursorHidden_ = false; } }

    // ---------- size ----------
    void refreshSize(bool force) {
        winsize ws{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col) {
            int r = ws.ws_row, c = ws.ws_col;
            if (force || r != lastRows_ || c != lastCols_) {
                lastRows_ = r;
                lastCols_ = c;
                rows = r;
                cols = c;
                if (!force) resized = true;
            }
        }
    }

    // ================= FRAME POLL =================
    void pollInput(int timeoutMs) {
        // Reset per-frame flags BEFORE reading new data
        keys = KeyState{};
        mouse.pressed    = false;
        mouse.released   = false;
        mouse.motion     = false;
        mouse.scrollUp   = false;
        mouse.scrollDown = false;
        resized = false;

        // Resolve leftover lone ESC from previous frame
        if (buf_.size() == 1 && (unsigned char)buf_[0] == 0x1b) {
            keys.escape = true;
            buf_.clear();
        }

        // Wait for input
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        int r = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char buf[8192];
            ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
            for (ssize_t i = 0; i < n; ++i)
                feed((unsigned char)buf[i]);
        }

        refreshSize(false);
    }

    // ================= PARSER =================
    void feed(unsigned char c) {
        buf_.push_back((char)c);
        parseBuffer();
    }

    void parseBuffer() {
        while (!buf_.empty()) {
            unsigned char first = (unsigned char)buf_[0];

            // ---- ESC-prefixed sequences ----
            if (first == 0x1b) {
                if (buf_.size() == 1) return;   // wait for continuation
                unsigned char second = (unsigned char)buf_[1];

                // CSI: ESC [ ...
                if (second == '[') {
                    if (buf_.size() < 3) return;

                    // SGR mouse: ESC [ < b ; x ; y M|m
                    if ((unsigned char)buf_[2] == '<') {
                        size_t end = buf_.find_first_of("Mm", 3);
                        if (end == std::string::npos) return;
                        int b = 0, x = 0, y = 0;
                        if (sscanf(buf_.c_str(), "\x1b[<%d;%d;%d", &b, &x, &y) == 3)
                            onMouse(b, x, y, buf_[end] == 'M');
                        buf_.erase(0, end + 1);
                        continue;
                    }

                    // Generic CSI: params then final byte 0x40..0x7E
                    size_t end = 2;
                    while (end < buf_.size()) {
                        unsigned char b = (unsigned char)buf_[end];
                        if (b >= 0x40 && b <= 0x7E) break;
                        ++end;
                    }
                    if (end >= buf_.size()) return;
                    onCSI(buf_.substr(2, end - 2), buf_[end]);
                    buf_.erase(0, end + 1);
                    continue;
                }

                // SS3: ESC O <char>
                if (second == 'O') {
                    if (buf_.size() < 3) return;
                    onSS3(buf_[2]);
                    buf_.erase(0, 3);
                    continue;
                }

                // Alt + printable
                keys.alt = true;
                onPrintable(second);
                buf_.erase(0, 2);
                continue;
            }

            // ---- single byte events ----
            if (first == 0x03) { keys.ctrl = true; keys.c = true; quit = true; buf_.erase(0,1); continue; }
            if (first == '\r' || first == '\n') { keys.enter = true; buf_.erase(0,1); continue; }
            if (first == '\t')                  { keys.tab   = true; buf_.erase(0,1); continue; }
            if (first == 0x7f || first == 0x08) { keys.backspace = true; buf_.erase(0,1); continue; }

            // Ctrl + letter (0x01..0x1A)
            if (first >= 0x01 && first <= 0x1a) {
                keys.ctrl = true;
                setLetter((char)('a' + first - 1));
                buf_.erase(0,1);
                continue;
            }

            // Printable ASCII
            if (first >= 0x20 && first < 0x7f) {
                onPrintable(first);
                buf_.erase(0,1);
                continue;
            }

            // UTF-8 lead byte → skip full codepoint
            if ((first & 0xE0) == 0xC0 || (first & 0xF0) == 0xE0 || (first & 0xF8) == 0xF0) {
                size_t need = 1;
                if ((first & 0xE0) == 0xC0) need = 2;
                else if ((first & 0xF0) == 0xE0) need = 3;
                else if ((first & 0xF8) == 0xF0) need = 4;
                if (buf_.size() < need) return;
                buf_.erase(0, need);
                continue;
            }

            // Unknown byte → drop
            buf_.erase(0,1);
        }
    }

    void onPrintable(unsigned char c) {
        keys.lastChar = (char)c;
        if (c == ' ') keys.space = true;
        setLetter((char)c);
    }

    void setLetter(char c) {
        switch (c) {
            case 'a': case 'A': keys.a = true; break;
            case 'b': case 'B': keys.b = true; break;
            case 'c': case 'C': keys.c = true; break;
            case 'd': case 'D': keys.d = true; break;
            case 'e': case 'E': keys.e = true; break;
            case 'f': case 'F': keys.f = true; break;
            case 'g': case 'G': keys.g = true; break;
            case 'h': case 'H': keys.h = true; break;
            case 'i': case 'I': keys.i = true; break;
            case 'j': case 'J': keys.j = true; break;
            case 'k': case 'K': keys.k = true; break;
            case 'l': case 'L': keys.l = true; break;
            case 'm': case 'M': keys.m = true; break;
            case 'n': case 'N': keys.n = true; break;
            case 'o': case 'O': keys.o = true; break;
            case 'p': case 'P': keys.p = true; break;
            case 'q': case 'Q': keys.q = true; break;
            case 'r': case 'R': keys.r = true; break;
            case 's': case 'S': keys.s = true; break;
            case 't': case 'T': keys.t = true; break;
            case 'u': case 'U': keys.u = true; break;
            case 'v': case 'V': keys.v = true; break;
            case 'w': case 'W': keys.w = true; break;
            case 'x': case 'X': keys.x = true; break;
            case 'y': case 'Y': keys.y = true; break;
            case 'z': case 'Z': keys.z = true; break;
            case '0': keys.n0 = true; break;
            case '1': keys.n1 = true; break;
            case '2': keys.n2 = true; break;
            case '3': keys.n3 = true; break;
            case '4': keys.n4 = true; break;
            case '5': keys.n5 = true; break;
            case '6': keys.n6 = true; break;
            case '7': keys.n7 = true; break;
            case '8': keys.n8 = true; break;
            case '9': keys.n9 = true; break;
            default: break;
        }
    }

    void onCSI(const std::string& params, char final) {
        // modifiers encoded as "1;N" where N-1 is a bitmask
        auto semi = params.find(';');
        if (semi != std::string::npos) {
            int m = atoi(params.c_str() + semi + 1);
            if (m > 1) {
                int x = m - 1;
                if (x & 1) keys.shift = true;
                if (x & 2) keys.alt   = true;
                if (x & 4) keys.ctrl  = true;
            }
        }
        std::string first = (semi == std::string::npos) ? params : params.substr(0, semi);
        int num = atoi(first.c_str());

        switch (final) {
            case 'A': keys.up    = true; break;
            case 'B': keys.down  = true; break;
            case 'C': keys.right = true; break;
            case 'D': keys.left  = true; break;
            case 'H': keys.home  = true; break;
            case 'F': keys.end   = true; break;
            case 'Z': keys.tab   = true; keys.shift = true; break;
            case '~':
                switch (num) {
                    case 1: case 7:  keys.home     = true; break;
                    case 2:          keys.insert   = true; break;
                    case 3:          keys.del      = true; break;
                    case 4: case 8:  keys.end      = true; break;
                    case 5:          keys.pageUp   = true; break;
                    case 6:          keys.pageDown = true; break;
                    case 11: keys.f1  = true; break;
                    case 12: keys.f2  = true; break;
                    case 13: keys.f3  = true; break;
                    case 14: keys.f4  = true; break;
                    case 15: keys.f5  = true; break;
                    case 17: keys.f6  = true; break;
                    case 18: keys.f7  = true; break;
                    case 19: keys.f8  = true; break;
                    case 20: keys.f9  = true; break;
                    case 21: keys.f10 = true; break;
                    case 23: keys.f11 = true; break;
                    case 24: keys.f12 = true; break;
                    default: break;
                }
                break;
            default: break;
        }
    }

    void onSS3(char c) {
        switch (c) {
            case 'A': keys.up    = true; break;
            case 'B': keys.down  = true; break;
            case 'C': keys.right = true; break;
            case 'D': keys.left  = true; break;
            case 'H': keys.home  = true; break;
            case 'F': keys.end   = true; break;
            case 'P': keys.f1    = true; break;
            case 'Q': keys.f2    = true; break;
            case 'R': keys.f3    = true; break;
            case 'S': keys.f4    = true; break;
            default: break;
        }
    }

    void onMouse(int b, int x, int y, bool press) {
        mouse.x = x - 1;
        mouse.y = y - 1;
        keys.shift = keys.shift || ((b & 4)  != 0);
        keys.alt   = keys.alt   || ((b & 8)  != 0);
        keys.ctrl  = keys.ctrl  || ((b & 16) != 0);

        int btn     = b & 3;
        bool motion = (b & 32) != 0;

        if (motion) { mouse.motion = true; return; }

        if (b & 64) {   // scroll wheel
            if (btn == 0) mouse.scrollUp   = true;
            else          mouse.scrollDown = true;
            return;
        }

        // button press/release
        bool p = press;
        switch (btn) {
            case 0: mouse.left   = p; break;
            case 1: mouse.middle = p; break;
            case 2: mouse.right  = p; break;
            default: break;
        }
        if (p) mouse.pressed  = true;
        else   mouse.released = true;
    }
};

// // ================= DEMO =================
// int main() {
//     Terminal t;
//     if (!t.enableRaw()) {
//         std::fprintf(stderr, "not a tty\n");
//         return 1;
//     }
//     t.enableMouse();
//     t.clear();

//     while (!t.quit) {
//         t.update();

//         if (t.keys.escape) t.write("ESC\r\n");
//         if (t.keys.enter)  t.write("ENTER\r\n");
//         if (t.keys.up)     t.write("UP\r\n");
//         if (t.keys.down)   t.write("DOWN\r\n");
//         if (t.keys.left)   t.write("LEFT\r\n");
//         if (t.keys.right)  t.write("RIGHT\r\n");
//         if (t.keys.a)      t.write("A\r\n");

//         // held state (for drag / hold logic)
//         if (t.mouse.left)  t.write("LEFT HELD\r\n");
//         if (t.mouse.right) t.write("RIGHT HELD\r\n");

//         // edge events (for click logic)
//         if (t.mouse.pressed)   t.write("MOUSE DOWN\r\n");
//         if (t.mouse.released)  t.write("MOUSE UP\r\n");

//         if (t.mouse.motion)    t.write("MOUSE MOVE\r\n");
//         if (t.mouse.scrollUp)  t.write("SCROLL UP\r\n");
//         if (t.mouse.scrollDown)t.write("SCROLL DOWN\r\n");
//     }

//     t.disableRaw();
//     return 0;
// }