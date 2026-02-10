# Nazg Prompt Enhancement Plan
**Version:** 1.0
**Date:** 2025-10-09
**Status:** Design Phase

---

## 1. Executive Summary

Transform nazg's prompt system from basic terminal output into a beautiful, modern CLI experience that adapts to terminal capabilities while maintaining functionality on minimal systems.

**Key Goals:**
- Beautiful, modern visual design that doesn't feel like "terminal output"
- Graceful degradation from 24-bit RGB → 256 colors → 16 colors → no colors
- Honor terminal capabilities (true color, Unicode, dimensions)
- Consistent visual language across all nazg commands
- New components: progress bars, spinners, themes, gradients
- Zero external dependencies

---

## 2. Current State Assessment

### ✅ What Works
- Basic box drawing with Unicode characters
- 3 style levels (MINIMAL, STANDARD, VERBOSE)
- Simple 8-color ANSI support
- Prompt types: confirm, choice, input
- Icon system with ASCII fallback
- Terminal width detection

### ❌ What Needs Improvement
- **Box Drawing:** Incomplete borders (missing right edge), no padding calculation for ANSI codes
- **Color System:** Only 8 basic colors, no gradients, no themes, no RGB support
- **Limited Components:** No progress bars, no spinners, no tables, no multi-select
- **Visual Design:** Feels like basic terminal output, not modern CLI
- **Terminal Detection:** Split across modules, incomplete capability detection

### ✅ Foundation Complete (Phase 0)
- Enhanced `system::terminal` module with comprehensive capability detection
- Color support levels: NONE → 8 → 16 → 256 → TRUE_COLOR
- Unicode detection with LC_ALL/LANG checking
- True color RGB functions: `ansi_rgb_fg()`, `ansi_rgb_bg()`
- 256-color palette functions: `ansi_256_fg()`, `ansi_256_bg()`
- Caching system for performance

---

## 3. Design Principles

### Visual Design
1. **Modern & Polished** - Should look like tools from 2025, not 1985
2. **Subtle & Professional** - Colors enhance, not distract
3. **Hierarchical** - Important info stands out, details recede
4. **Consistent** - Same patterns across all of nazg
5. **Purposeful Color** - Every color has meaning (green = success, red = error, etc.)

### Technical Design
1. **Graceful Degradation** - Works on any terminal, beautiful on modern ones
2. **Performance** - Minimal overhead, cached capabilities
3. **Composable** - Components combine cleanly
4. **Type-Safe** - Leverage C++ type system
5. **No Dependencies** - Pure ANSI escape codes

---

## 4. Architecture

### Module Structure
```
nazg/
├── modules/
│   ├── system/
│   │   ├── include/system/terminal.hpp  [✅ ENHANCED]
│   │   └── src/terminal.cpp             [✅ ENHANCED]
│   │
│   └── prompt/
│       ├── include/prompt/
│       │   ├── prompt.hpp               [🔄 UPDATE]
│       │   ├── colors.hpp               [🔄 ENHANCE]
│       │   ├── icons.hpp                [✅ GOOD]
│       │   ├── theme.hpp                [✨ NEW]
│       │   ├── box.hpp                  [✨ NEW]
│       │   ├── progress.hpp             [✨ NEW]
│       │   └── gradient.hpp             [✨ NEW]
│       │
│       └── src/
│           ├── prompt.cpp               [🔄 UPDATE]
│           ├── colors.cpp               [🔄 ENHANCE]
│           ├── icons.cpp                [✅ GOOD]
│           ├── theme.cpp                [✨ NEW]
│           ├── box.cpp                  [✨ NEW]
│           ├── progress.cpp             [✨ NEW]
│           └── gradient.cpp             [✨ NEW]
```

### Component Hierarchy
```
ColorFormatter
    ├── Uses system::terminal capabilities
    └── Provides RGB, 256, 16, 8 color output

Theme
    ├── Uses ColorFormatter
    └── Provides named colors (primary, accent, error, etc.)

Gradient
    ├── Uses ColorFormatter
    └── Generates smooth color transitions

BoxStyle
    ├── Uses system::terminal Unicode support
    └── Provides border styles (rounded, double, thick, etc.)

Box
    ├── Uses BoxStyle, Theme, ColorFormatter
    ├── Handles ANSI code stripping for padding
    └── Renders bordered content

ProgressBar
    ├── Uses Theme, Gradient, Box
    └── Animated progress visualization

Prompt
    ├── Uses all components
    └── High-level user interaction API
```

---

## 5. Phased Implementation Plan

### Phase 1: Enhanced Color System
**Goal:** Add RGB, 256-color, gradients, ANSI stripping utilities
**Test:** Color capability detection, color output, gradient generation
**Files:** `colors.hpp`, `colors.cpp`, `gradient.hpp`, `gradient.cpp`

**Features:**
- Enhance `ColorFormatter` with RGB and 256-color support
- Add ANSI code length calculation (for padding)
- Add ANSI code stripping utility
- Create `Gradient` class for smooth color transitions
- Add color conversion utilities (RGB → 256 → 16 → 8)
- Add color interpolation for gradients

**Deliverables:**
```cpp
// Enhanced ColorFormatter
ColorFormatter::rgb(r, g, b, text)
ColorFormatter::hex(color, text)        // "#5e81ac"
ColorFormatter::c256(color, text)       // 0-255
ColorFormatter::gradient(start, end, steps)

// Utilities
size_t ansi_display_width(const string& text)
string strip_ansi_codes(const string& text)
```

**Testing:**
- Create test command: `nazg test prompt colors`
- Show color gradients, capability detection output
- Test on NO_COLOR, FORCE_COLOR environments

---

### Phase 2: Visual Themes
**Goal:** Named color schemes that adapt to terminal capabilities
**Test:** Switch between themes, see consistent colors
**Files:** `theme.hpp`, `theme.cpp`

**Features:**
- `Theme` class with semantic color names
- Built-in themes: Nord, Dracula, OneDark, Solarized, GitHub
- Theme auto-adapts to terminal capabilities
- Theme registry for custom themes

**Theme Color Palette:**
```cpp
struct Theme {
  Color primary;      // Main brand color (nazg identity)
  Color accent;       // Highlights, links
  Color success;      // Green checkmarks, success states
  Color warning;      // Yellow warnings
  Color error;        // Red errors
  Color info;         // Blue informational
  Color muted;        // Gray/dim secondary text
  Color text;         // Primary text
  Color background;   // Background (for box fills)
  Color border;       // Box borders
};
```

**Built-in Themes:**
1. **Nord** (default) - Arctic, blue-tinted, professional
2. **Dracula** - Dark, purple-tinted, vibrant
3. **OneDark** - Atom-inspired, balanced
4. **Solarized** - Light/dark variants, proven palette
5. **GitHub** - Clean, recognizable

**Deliverables:**
```cpp
Theme::nord()
Theme::dracula()
Theme::onedark()
Theme::from_name("nord")
theme.primary("text")
theme.success("✓ Complete")
```

**Testing:**
- Create test command: `nazg test prompt themes`
- Show all themes side-by-side
- Test theme degradation (RGB → 256 → 16)

---

### Phase 3: Fixed Box Drawing
**Goal:** Complete, beautiful boxes with proper padding
**Test:** Boxes render correctly, padding aligns with ANSI codes
**Files:** `box.hpp`, `box.cpp`, update `prompt.cpp`

**Features:**
- Multiple box styles (rounded, double, thick, shadow, minimal)
- Complete borders (all 4 sides + corners)
- Proper padding calculation (strips ANSI for width)
- Configurable padding, margins
- Title rendering (top-left, top-center, top-right)
- Multi-line content with proper wrapping
- Optional background colors
- Shadow effect for depth

**Box Styles:**
```
┌─────────┐  ╔═════════╗  ╭─────────╮  ┏━━━━━━━━━┓  ┌─────────┐░
│ Rounded │  ║ Double  ║  │  Heavy  │  ┃  Thick  ┃  │  Shadow │░
└─────────┘  ╚═════════╝  ╰─────────╯  ┗━━━━━━━━━┛  └─────────┘░
                                                      ░░░░░░░░░░░
```

**Deliverables:**
```cpp
Box box(BoxStyle::ROUNDED);
box.title("nazg • git-init")
   .padding(2)
   .width(60)
   .theme(Theme::nord())
   .background(true)
   .shadow(true);
box.line("Content here");
box.render();
```

**Testing:**
- Create test command: `nazg test prompt boxes`
- Show all box styles
- Test with long text, ANSI codes, Unicode
- Verify padding alignment

---

### Phase 4: Progress Bars
**Goal:** Beautiful animated progress visualization
**Test:** Progress bars update smoothly, show percentages
**Files:** `progress.hpp`, `progress.cpp`

**Features:**
- Multiple progress styles (bar, dots, spinner, percentage)
- Gradient fills (theme-aware)
- Optional label, percentage, ETA
- Smooth animations
- Indeterminate progress (spinner)
- Multi-bar support (for parallel tasks)

**Progress Styles:**
```
[████████████░░░░░░░░] 60% Compiling...

●●●●●●○○○○ 60% Compiling...

⠋ Compiling... (spinner)

60% ━━━━━━━━━━━━━━╸━━━━━━━━━━ 12.3s / 20.5s
```

**Deliverables:**
```cpp
ProgressBar progress("Compiling", 100);
progress.style(ProgressStyle::BAR)
        .theme(Theme::nord())
        .gradient(true)
        .show_percentage(true)
        .show_eta(true);

progress.update(50);  // Update to 50%
progress.finish();    // Complete
```

**Testing:**
- Create test command: `nazg test prompt progress`
- Show all progress styles
- Animate progress from 0% → 100%
- Test spinner animation

---

### Phase 5: Integration & Polish
**Goal:** Update all prompts, consistent visual language
**Test:** All nazg commands use new system
**Files:** Update all command files using Prompt

**Features:**
- Update `Prompt` class to use new Box, Theme, Progress
- Apply consistent styling to all prompts
- Add theme configuration option
- Add `nazg config set prompt.theme <name>`
- Create visual consistency guidelines
- Performance optimization

**Visual Language Rules:**
```
1. Borders: Rounded boxes for standard, minimal for minimal style
2. Colors:
   - Primary (nord4): nazg branding, titles
   - Success (nord14): Checkmarks, success states
   - Error (nord11): Error states, warnings
   - Accent (nord8): Highlights, important info
   - Muted (nord3): Secondary text, context
3. Spacing:
   - 1 line between sections
   - 2 spaces indent for nested content
4. Progress:
   - Gradient bars for long operations
   - Spinners for indeterminate progress
5. Icons:
   - ✓ Success (green)
   - ✗ Error (red)
   - ⚠ Warning (yellow)
   - ℹ Info (blue)
   - → Arrow (primary)
```

**Deliverables:**
- Updated `git init` prompt with new styling
- Updated `git status` with themed colors
- Updated test runner with progress bars
- Configuration system for themes
- Documentation with visual examples

**Testing:**
- Test all major commands
- Verify visual consistency
- Test theme switching
- Performance benchmarks

---

## 6. Component Specifications

### 6.1 ColorFormatter (Enhanced)

```cpp
class ColorFormatter {
public:
  ColorFormatter(const TerminalCapabilities& caps);

  // RGB colors (auto-adapts to capabilities)
  string rgb(int r, int g, int b, const string& text) const;
  string rgb_bg(int r, int g, int b, const string& text) const;

  // Hex colors
  string hex(const string& color, const string& text) const;  // "#5e81ac"
  string hex_bg(const string& color, const string& text) const;

  // 256-color palette
  string c256(int color, const string& text) const;
  string c256_bg(int color, const string& text) const;

  // Gradient generation
  vector<string> gradient(const string& start_hex,
                          const string& end_hex,
                          int steps) const;

  // Existing methods remain...
  string green(const string& text) const;
  string red(const string& text) const;
  // ... etc

  // Utility
  static size_t display_width(const string& text);
  static string strip_ansi(const string& text);

private:
  ColorSupport capability_;
  string apply_color(int r, int g, int b, bool bg, const string& text) const;
  tuple<int,int,int> rgb_to_256(int r, int g, int b) const;
  tuple<int,int,int> rgb_to_16(int r, int g, int b) const;
};
```

### 6.2 Theme

```cpp
struct Color {
  int r, g, b;
  string hex() const;
  static Color from_hex(const string& hex);
};

struct Theme {
  string name;
  Color primary;
  Color accent;
  Color success;
  Color warning;
  Color error;
  Color info;
  Color muted;
  Color text;
  Color background;
  Color border;

  // Built-in themes
  static Theme nord();
  static Theme dracula();
  static Theme onedark();
  static Theme solarized_dark();
  static Theme solarized_light();
  static Theme github();

  // Apply color with formatter
  string primary(const string& text, const ColorFormatter& fmt) const;
  string success(const string& text, const ColorFormatter& fmt) const;
  // ... etc

  // Registry
  static Theme from_name(const string& name);
  static vector<string> available_themes();
};
```

### 6.3 Box

```cpp
enum class BoxStyle {
  ROUNDED,    // ╭─╮│╰─╯
  DOUBLE,     // ╔═╗║╚═╝
  HEAVY,      // ┏━┓┃┗━┛
  CLASSIC,    // ┌─┐│└─┘
  MINIMAL,    // Thin lines
  NONE        // No borders
};

class Box {
public:
  Box(BoxStyle style = BoxStyle::ROUNDED);

  // Configuration
  Box& title(const string& text, Alignment align = Alignment::LEFT);
  Box& width(int w);
  Box& padding(int p);
  Box& theme(const Theme& t);
  Box& background(bool enable);
  Box& shadow(bool enable);

  // Content
  Box& line(const string& text);
  Box& separator();
  Box& empty();

  // Rendering
  void render() const;
  string to_string() const;

private:
  BoxStyle style_;
  Theme theme_;
  vector<string> lines_;
  string title_;
  int width_;
  int padding_;
  bool background_;
  bool shadow_;
  ColorFormatter formatter_;

  string get_border_chars() const;
  string pad_line(const string& content) const;
};
```

### 6.4 ProgressBar

```cpp
enum class ProgressStyle {
  BAR,         // [████░░░░]
  DOTS,        // ●●●○○○
  GRADIENT,    // Gradient-filled bar
  PERCENTAGE,  // Just percentage
  SPINNER      // Indeterminate spinner
};

class ProgressBar {
public:
  ProgressBar(const string& label, int total);

  // Configuration
  ProgressBar& style(ProgressStyle s);
  ProgressBar& theme(const Theme& t);
  ProgressBar& gradient(bool enable);
  ProgressBar& show_percentage(bool show);
  ProgressBar& show_eta(bool show);
  ProgressBar& width(int w);

  // Updates
  void update(int current);
  void increment(int delta = 1);
  void finish();
  void fail();

  // Spinner (for indeterminate)
  void spin();

private:
  string label_;
  int current_;
  int total_;
  ProgressStyle style_;
  Theme theme_;
  ColorFormatter formatter_;

  void render() const;
  string render_bar() const;
  string render_dots() const;
  string render_spinner() const;
};
```

### 6.5 Gradient

```cpp
class Gradient {
public:
  Gradient(const Color& start, const Color& end, int steps);

  // Generate gradient
  vector<Color> colors() const;
  vector<string> apply(const string& text, const ColorFormatter& fmt) const;

  // Presets
  static Gradient rainbow(int steps);
  static Gradient fire(int steps);
  static Gradient ocean(int steps);
  static Gradient forest(int steps);
  static Gradient sunset(int steps);

private:
  Color start_;
  Color end_;
  int steps_;

  Color interpolate(float t) const;
};
```

---

## 7. Visual Design Mockups

### Before (Current)
```
┌─
│ nazg • git-init
│ Directory: /home/user/project
│ Branch: main
│ Language: C++
├─
│ Initialize git repository?
│
│   • Initialize git repository (main branch)
│   • Generate .gitignore for C++
│   • Set git identity: user <user@localhost>
│   • Create initial commit
│
└─> (Y/n)
```

### After (Phase 5 Complete)
```
╭────────────────────────────────────────────────────────────╮
│ nazg • git-init                                            │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  Directory: /home/user/project                            │
│  Branch:    main                                          │
│  Language:  C++                                           │
│                                                            │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  Initialize git repository?                               │
│                                                            │
│    • Initialize git repository (main branch)              │
│    • Generate .gitignore for C++                          │
│    • Set git identity: user <user@localhost>              │
│    • Create initial commit                                │
│                                                            │
╰────────────────────────────────────────────────────────────╯
→ (Y/n)
```

With colors (Nord theme, true color terminal):
- Title: Bright cyan (#88C0D0)
- Border: Dim blue-gray (#434C5E)
- Labels: Dim gray (#D8DEE9)
- Values: Bright white (#ECEFF4)
- Bullets: Bright cyan (#88C0D0)
- Background: Subtle dark gray (#2E3440) fill

### Progress Bar Example
```
Compiling project...
━━━━━━━━━━━━━━━━━━╸━━━━━━━━━━━━━━━━━━━━━━ 45% • 12.3s / 27.5s

(With gradient: dark cyan → bright cyan → white at progress edge)
```

---

## 8. Testing Strategy

### Per-Phase Testing
Each phase includes a dedicated test command:
```bash
nazg test prompt colors    # Phase 1
nazg test prompt themes    # Phase 2
nazg test prompt boxes     # Phase 3
nazg test prompt progress  # Phase 4
nazg test prompt all       # All components
```

### Terminal Compatibility Matrix
Test on:
- **True color terminals:** Kitty, Alacritty, WezTerm, iTerm2
- **256-color terminals:** xterm-256color, screen-256color
- **16-color terminals:** xterm, Linux console
- **No-color:** `NO_COLOR=1`
- **Non-TTY:** Piped output
- **Unicode/ASCII:** UTF-8 vs ASCII-only locales

### Performance Benchmarks
- Prompt rendering: < 1ms
- Progress bar updates: < 0.1ms
- Theme switching: < 0.1ms
- Gradient generation: < 1ms

### Visual Regression
- Screenshot tests for each theme
- Compare output on different terminal capabilities
- Verify degradation is graceful

---

## 9. Configuration

Add theme configuration to nazg:
```bash
# Set theme
nazg config set prompt.theme nord

# Disable colors
nazg config set prompt.colors false

# Force color level
nazg config set prompt.color-level true-color  # true-color|256|16|8|none

# Disable Unicode
nazg config set prompt.unicode false
```

Environment variables (for testing):
```bash
NO_COLOR=1              # Disable all colors
FORCE_COLOR=3           # Force true color
NAZG_THEME=dracula     # Override theme
NO_UNICODE=1           # Disable Unicode
```

---

## 10. Documentation

Create documentation:
- `docs/prompt_system.md` - Developer guide
- `docs/themes.md` - Theme specification
- `docs/visual_guide.md` - Visual consistency rules
- Examples in `modules/prompt/src/demo.cpp`

---

## 11. Success Metrics

**Visual Quality:**
- [ ] Prompts look modern and polished
- [ ] Colors enhance without distracting
- [ ] Consistent visual language across nazg
- [ ] Graceful degradation on all terminals

**Technical Quality:**
- [ ] Zero external dependencies
- [ ] Performance: < 1ms rendering
- [ ] Works on Windows, macOS, Linux
- [ ] No regressions in existing functionality

**User Experience:**
- [ ] Users say "wow, this looks great"
- [ ] Clear hierarchy of information
- [ ] Easy to scan and understand
- [ ] Accessible (color-blind friendly themes)

---

## 12. Timeline Estimate

**Phase 0:** ✅ Complete (Terminal detection)
**Phase 1:** 2-3 hours (Color system)
**Phase 2:** 1-2 hours (Themes)
**Phase 3:** 2-3 hours (Box drawing)
**Phase 4:** 2-3 hours (Progress bars)
**Phase 5:** 3-4 hours (Integration)

**Total:** ~12-15 hours of implementation + testing

---

## 13. Open Questions

1. **Theme Storage:** Store custom themes in config.toml or separate file?
2. **Animation:** Should progress bars animate automatically or require manual updates?
3. **Accessibility:** Add high-contrast theme? Screen reader support?
4. **Icons:** Expand icon set? Nerd Font support?
5. **Internationalization:** RTL language support for boxes?

---

## 14. Future Enhancements (Post-v1)

- **Interactive menus:** Arrow key navigation
- **Multi-select:** Checkbox lists
- **Tables:** Grid layout with borders
- **Forms:** Multi-field input with validation
- **Notifications:** Toast-style messages
- **Charts:** Bar charts, sparklines
- **Syntax highlighting:** Code blocks in prompts
- **Markdown rendering:** Rich text in prompts

---

## Appendix A: Color Palettes

### Nord
```
nord0:  #2E3440  (background)
nord1:  #3B4252  (darker background)
nord2:  #434C5E  (borders)
nord3:  #4C566A  (muted)
nord4:  #D8DEE9  (text)
nord5:  #E5E9F0  (bright text)
nord6:  #ECEFF4  (brightest)
nord7:  #8FBCBB  (info)
nord8:  #88C0D0  (primary/accent)
nord9:  #81A1C1  (secondary)
nord10: #5E81AC  (tertiary)
nord11: #BF616A  (error)
nord12: #D08770  (warning/orange)
nord13: #EBCB8B  (warning/yellow)
nord14: #A3BE8C  (success)
nord15: #B48EAD  (purple)
```

### Dracula
```
background: #282a36
current:    #44475a
foreground: #f8f8f2
comment:    #6272a4
cyan:       #8be9fd  (primary)
green:      #50fa7b  (success)
orange:     #ffb86c  (warning)
pink:       #ff79c6  (accent)
purple:     #bd93f9  (info)
red:        #ff5555  (error)
yellow:     #f1fa8c  (warning alt)
```

---

**END OF PLAN**
