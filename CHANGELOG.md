# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased] — Root Intelligence UI

> **Codename:** Root Intelligence
> **Base:** OpenPrinting CUPS 2.5b1
> **Scope:** Frontend / Web Administration Interface only
> **Backend compatibility:** 100% preserved — no CGI, IPP or build system logic was altered.

### Added

#### Design System (`doc/cups.css`)
- Dark glassmorphism theme with `backdrop-filter: blur(14px)` and semi-transparent panels
- Cohesive color palette: deep navy `#0d1117` background + sage green `#a8c686` accent
- CSS custom properties (`--ri-*`) for consistent, easy theming across all components
- Smooth transitions (`0.3s cubic-bezier(0.4, 0, 0.2, 1)`) on all interactive elements
- Subtle glow borders (`box-shadow: 0 0 16px rgba(168, 198, 134, 0.22)`) on focus/hover
- Animated status badges with pulse effect for printer states (idle, processing, stopped)
- Full responsive layout covering 360px (mobile) → 768px (tablet) → 1440px+ (desktop)

#### JavaScript Interactive Layer (`doc/cups.js` — **new file**)
- **Zero external dependencies** — runs fully offline, suitable for air-gapped Linux servers
- **User Mode Toggle** (Beginner ↔ Advanced) persisted via `localStorage`
  - Beginner mode: hides advanced panels, shows contextual help tooltips
  - Advanced mode: reveals telemetry bar, raw command shortcuts, diagnostic info
- **Onboarding Checklist** with animated progress bar for first-time users
  - 3 guided steps: verify daemon, add printer, print test page
  - State persisted in `localStorage`; dismissable permanently
- **Guided Tour Modal** — interactive 3-step walkthrough explaining:
  1. CUPS architecture and the role of the web interface
  2. Local diagnostics without leaving the browser
  3. Power-user shortcuts and command reference
- **Command Palette** (`Ctrl+K` / `⌘K`) — Raycast/Spotlight-style launcher
  - Real-time fuzzy search across: quick actions, print queues, terminal commands
  - Keyboard navigation with `↑` `↓`, `Enter` to execute, `Esc` to close
  - Copy-to-clipboard for shell commands (`lpstat -p -d`, `cupsenable`, `cancel -a`)
  - Mouse-friendly with hover highlight and click activation

#### Template Enhancements (`templates/` + `templates/pt_BR/`)
- `header.tmpl`: Command Palette trigger button + Mode Switch toggle in navigation bar
- `home.tmpl`: Onboarding banner + Advanced telemetry status bar
- `printers.tmpl` / `printer.tmpl`: Animated printer cards with status glow indicators
- `jobs.tmpl` / `jobs-header.tmpl`: Modernized job list with inline action buttons
- `admin.tmpl`: Redesigned admin panel with section cards and descriptive labels
- `classes.tmpl` / `class.tmpl`: Consistent dark card treatment for printer classes
- `error.tmpl`: Friendly error page with contextual recovery suggestions
- `search.tmpl`: Styled search results with highlighted matches
- `trailer.tmpl`: Updated footer with version reference and keyboard shortcut hint
- All templates: accessible focus rings, semantic HTML improvements, ARIA labels

#### Developer Tooling (`tools/` — **new directory**)
- `cups_preview_server.py`: Local Python HTTP server simulating CUPS CGI routing
- `cups_template.py`: Lightweight template engine rendering `.tmpl` files offline
- `capture_step4.py` / `capture_step5.py`: Automated Playwright screenshot scripts
- `capture_all_screens.py`: Batch visual regression capture (Desktop/Tablet/Mobile)

### Changed
- `doc/Makefile`: Added `cups.js` to `WEBPAGES` install target
- `doc/cups.css`: Extended with Section 11 (Root Intelligence additions), leaving original sections intact
- `doc/cups-printable.css`: Minor alignment fixes for print stylesheet

### Not Changed (Intentionally)
- All CGI backend source files (`scheduler/`, `cgi-bin/`, `cups/`)
- Template variable interpolation syntax (`{variable}`, `{#array=0?then:else}`)
- Form `OP` parameter routing and `org.cups.sid` CSRF token handling
- `configure.ac`, `Makefile.in`, autoconf build pipeline
- IPP protocol handling and PPD driver logic

---

## Previous Releases

For the upstream OpenPrinting CUPS changelog, see:
https://github.com/OpenPrinting/cups/blob/master/CHANGES.md
