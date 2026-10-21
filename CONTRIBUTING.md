# Contributing to CUPS — Root Intelligence UI

Thank you for your interest in improving the **Root Intelligence** frontend for
OpenPrinting CUPS! This document describes conventions, file layout, and the
development workflow used in this fork.

> **Scope of this fork:** Only the web administration frontend is modified here.
> For contributions to the CUPS printing backend, daemon, PPD handling or IPP
> protocol — please contribute directly to
> [OpenPrinting/cups](https://github.com/OpenPrinting/cups).

---

## Table of Contents

1. [Philosophy](#philosophy)
2. [Getting Started](#getting-started)
3. [Project Structure](#project-structure)
4. [Frontend Architecture](#frontend-architecture)
5. [Template Conventions](#template-conventions)
6. [CSS Conventions](#css-conventions)
7. [JavaScript Conventions](#javascript-conventions)
8. [Testing Locally](#testing-locally)
9. [Submitting a Pull Request](#submitting-a-pull-request)
10. [Code of Conduct](#code-of-conduct)

---

## Philosophy

> *"A clareza do terminal. A inteligência do contexto. O controle continua seu."*
> *("The clarity of the terminal. The intelligence of context. The control remains yours.")*

This fork values:

- **Autonomy** — The interface works fully offline; no CDN, no external fonts required.
- **Transparency** — Every action surfaced in the UI maps to a real CUPS/IPP operation.
- **Efficiency** — Power users shouldn't need more than 2 clicks or 1 keyboard shortcut.
- **Composability** — UI components are independent; removing one doesn't break others.
- **Beginner-friendliness** — Beginner Mode, Onboarding Checklist, and Tour Modal lower the barrier without condescending to experienced users.

---

## Getting Started

### Prerequisites

- Git
- Python 3.8+ (for the local preview server)
- A modern browser (Chrome/Firefox/Edge)
- Optional: Playwright for automated screenshots

### Clone and preview

```bash
git clone https://github.com/<your-username>/cups.git
cd cups

# Start the local preview server (no CUPS daemon needed)
python tools/cups_preview_server.py

# Open in browser
open http://localhost:8631
```

The preview server renders all `.tmpl` files with realistic fixture data so you
can iterate on the UI without compiling CUPS or running the daemon.

---

## Project Structure

```
cups/
├── doc/
│   ├── cups.css              # Main stylesheet (see CSS Conventions)
│   ├── cups.js               # JavaScript layer (see JavaScript Conventions)
│   ├── cups-printable.css    # Print stylesheet
│   └── Makefile              # Install targets (cups.js added here)
│
├── templates/                # English templates
│   ├── header.tmpl           # Global nav: mode toggle + command palette button
│   ├── home.tmpl             # Dashboard: onboarding banner + telemetry bar
│   ├── printers.tmpl         # Printer list
│   ├── printer.tmpl          # Printer detail
│   ├── jobs.tmpl             # Job list
│   ├── admin.tmpl            # Administration panel
│   └── ...
│
├── templates/pt_BR/          # Brazilian Portuguese translations (mirrors templates/)
│
└── tools/                    # Developer tooling (not shipped with CUPS)
    ├── cups_preview_server.py
    ├── cups_template.py
    ├── capture_step4.py
    ├── capture_step5.py
    └── capture_all_screens.py
```

---

## Frontend Architecture

The UI layer is a **progressive enhancement** stack:

```
HTML (templates/*.tmpl)       <- always works, even without JS
  └── CSS (doc/cups.css)      <- visual design; mode-visibility rules
        └── JS (doc/cups.js)  <- interactive features; mounts safely if DOM ready
```

**Key principle:** If `cups.js` fails to load or JS is disabled, the page remains
fully functional — the Command Palette trigger button simply won't respond, and
Mode Toggle will default to Beginner styling via CSS alone.

### Component inventory (cups.js)

| Export / init function | Description |
|---|---|
| `initModeToggle()` | Beginner vs Advanced switch, `localStorage` persistence |
| `initOnboarding()` | Checklist banner with animated progress bar |
| `initTour()` | 3-step guided tour modal |
| `initCommandPalette()` | Ctrl+K launcher with fuzzy search |
| `DOMContentLoaded` listener | Mounts all components after DOM is ready |

---

## Template Conventions

CUPS templates use a custom variable syntax — **do not break it**:

| Syntax | Meaning |
|---|---|
| `{variable}` | Interpolated string value |
| `{?condition}` ... `{/condition}` | Conditional block |
| `{#array=0?then:else}` | Array iteration |
| `{[localized string]}` | Localized string lookup |

**Rules:**
1. Never add Handlebars / Jinja2 / Mustache syntax — it breaks the CGI renderer.
2. Preserve `<input type="hidden" name="org.cups.sid" value="{org.cups.sid}">` on every form.
3. Keep `OP` parameter routing intact: `<input type="hidden" name="OP" value="...">`.
4. Mirror every change in `templates/` to `templates/pt_BR/` (translate user-visible strings).

---

## CSS Conventions

All Root Intelligence additions live in **Section 11** of `cups.css`, clearly delimited:

```css
/* ============================================================
   §11 — ROOT INTELLIGENCE (UI OVERHAUL)
   ============================================================ */
```

**Rules:**
- Use CSS custom properties defined in `:root` under §11 — never hard-code colors.
- Variable naming: `--ri-<category>-<variant>` e.g. `--ri-accent`, `--ri-surface-glass`.
- Prefix all new class names with `.ri-` to avoid collisions with legacy CUPS styles.
- Transitions must respect `prefers-reduced-motion`.
- Do not modify Sections 1–10 unless fixing a collision with legacy rules.

### Color palette reference

| Token | Value | Usage |
|---|---|---|
| `--ri-bg` | `#0d1117` | Page background |
| `--ri-surface` | `rgba(22,27,34,0.85)` | Card/panel backgrounds |
| `--ri-accent` | `#a8c686` | Primary accent (sage green) |
| `--ri-accent-glow` | `rgba(168,198,134,0.22)` | Glow shadows |
| `--ri-text` | `#e6edf3` | Primary text |
| `--ri-text-muted` | `#8b949e` | Secondary/muted text |
| `--ri-border` | `rgba(48,54,61,0.8)` | Borders |
| `--ri-error` | `#f85149` | Error states |
| `--ri-warning` | `#d29922` | Warning states |
| `--ri-success` | `#3fb950` | Success states |

---

## JavaScript Conventions

- **No external dependencies.** Do not import lodash, jQuery, Alpine.js, etc.
- **No build step required.** `cups.js` is vanilla ES2017.
- **Guard every DOM query:**

```js
const el = document.getElementById('ri-cmd-palette');
if (!el) return; // graceful no-op if template doesn't include this element
```

- **Use `localStorage` for persistence** — not cookies, not sessionStorage.
- **Key naming:** `cups.ri.<feature>` e.g. `cups.ri.mode`, `cups.ri.onboarding.done`.
- Add JSDoc comments to every exported function.

---

## Testing Locally

### Visual regression (automated)

```bash
# Install Playwright (one-time)
pip install playwright
playwright install chromium

# Run screenshot capture
python tools/capture_step5.py
```

### Manual checklist before opening a PR

- [ ] Preview server renders all pages without errors
- [ ] Beginner Mode hides advanced-only elements; Advanced Mode shows them
- [ ] Command Palette opens with `Ctrl+K` and closes with `Esc`
- [ ] Onboarding banner appears on first load and persists via localStorage
- [ ] Tour Modal navigates all 3 steps and closes cleanly
- [ ] No JavaScript errors in browser console
- [ ] `pt_BR` templates render correctly
- [ ] Mobile layout (360px) has no horizontal overflow
- [ ] `prefers-reduced-motion` disables all animations

---

## Submitting a Pull Request

1. **Fork** this repository and create a branch: `feat/your-feature-name`
2. **Follow conventions** described in this document
3. **Run the manual checklist** above
4. **Open a PR** with:
   - A clear title: `feat(ui): ...` / `fix(css): ...` / `docs: ...`
   - A short description of *what* changed and *why*
   - Screenshots or screen recording if your change is visual

### Commit message format

```
<type>(<scope>): <short summary>
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`
Scopes: `ui`, `css`, `js`, `template`, `tools`, `build`

---

## Code of Conduct

This project follows the
[Contributor Covenant v2.1](https://www.contributor-covenant.org/version/2/1/code_of_conduct/).

Be respectful. Be curious. Build tools that empower people.

---

*Root Intelligence is a community concept, not an official OpenPrinting product.
For upstream CUPS issues, please visit https://github.com/OpenPrinting/cups/issues.*
