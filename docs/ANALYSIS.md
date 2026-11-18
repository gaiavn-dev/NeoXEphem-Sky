# XEphem on Windows: reverse-engineering notes and historical-sky analysis

Source analysed: upstream XEphem (GitHub XEphem/XEphem, vendored in `third_party/xephem`, v4.3.0 plus ANSI-C
cleanup commits, MIT license).

## 1. How XEphem is built

| Part | Size | What it does | Portability |
|---|---|---|---|
| `libastro/` | ~27k lines C, 63 files | All astronomy: time scales, precession, nutation, aberration, refraction, VSOP87 / Chapront-95 planets, ELP-style Moon, Delta-T, comets/asteroids, satellites (SGP4/SDP4), constellations, rise/set, eclipses | **Pure ANSI C + stdio/math.** Only one non-portable line: `#include <unistd.h>` in `bdl.c`, which is unused |
| `GUI/xephem/` | ~105k lines C, 73 files | Motif/X11 GUI: Sky View, Solar System, Moon, Mars, Jupiter, Saturn, Earth, data tables, plotting, FITS images, INDI telescope control, web catalog downloads | Unix-only (see below) |
| `libip`, `liblilxml`, `libjpegd`, `libpng`, `libz` | small | image processing, XML (INDI), JPEG/PNG/zlib | portable C |
| `GUI/xephem/catalogs`, `auxil` | data | SKY2000 star catalog to mag 6.5 (8,997 stars), Messier, 525 sites, constellation figures | plain text (`.edb` format) |

### Unix/X11 dependencies of the GUI (measured)

| API | Usage |
|---|---|
| Motif widget creation (`XmCreate*`, `Xm*Create`) | **1,735 calls in 55 files** |
| Raw Xlib drawing (`XDraw*`, `XFillArc`, `XCopyArea`) | 155 calls in 22 files |
| `XtAppAddInput` event-loop I/O | 8 calls in 6 files |
| `fork` / `exec*` / `pipe` / `select` / `signal` / `popen` | about 32 calls (INDI, FIFOs, help browser, network) |
| OpenSSL `SSL_*` | 42 calls in 5 files (HTTPS catalog fetch) |

Conclusion: the **engine is portable as is**. The **GUI is welded to
Motif/Xt**. A faithful native port of every screen means rewriting the
whole user interface.

## 2. Ways to run XEphem on Windows

| Route | Effort | Result | Status |
|---|---|---|---|
| **A. Native engine + new native front end** (this folder) | Done for the Sky View | True `.exe`, no X server, starts instantly | ✅ Built and verified |
| B. WSL2 + WSLg (Windows 11) | ~15 min: `wsl --install -d Ubuntu-22.04`, then follow the INSTALL `apt install ...` line and `make` | The full original Motif app in a Windows window | Not run here: only the `docker-desktop` WSL distro is installed |
| C. Docker container from `Containers/docker-ubuntu-22.04` plus WSLg or VcXsrv | ~30 min | Same as B | Docker Desktop is installed but was stopped; not run |
| D. Cygwin (has `libXm`/Motif) plus Cygwin/X | 1–2 days of patching | Full app as `.exe`, but it needs an X server | Not attempted |
| E. Rewrite the whole GUI on Win32, Qt or Dear ImGui | Months (about 1,700 widget sites) | Fully native | Recommended only as the incremental continuation of A |

**Recommendation:** use **A** for a distributable Windows product, and grow
it screen by screen on top of `skycore`. Use **B** when you need a
not-yet-ported XEphem feature today.

### What was done for route A

* `CMakeLists.txt` compiles the upstream `libastro/*.c` **unmodified** with
  MSVC 2022. The only shim is `compat/unistd.h`.
* Two Windows clashes are handled on the NeoXEphem side, not by patching
  upstream:
  * `astro.h` `#define`s short names (`lat`, `lng`, `elev`, `mjd`,
    `temp`, `pressure`, ...), so the new code avoids those identifiers.
  * `wingdi.h` has `typedef FIXED`, which collides with libastro's
    `enum ObjType { FIXED, ... }`. `skywin.c` renames the Windows one while
    it includes `<windows.h>`.
* XEphem's regression test (`tests/test.c`) builds as `astrotest.exe`. Its
  output is **identical** to `tests/output.expected`, which proves the
  Windows build of the engine gives the same numbers as Linux.
* `skycore.c` reimplements what XEphem's Sky View does with the engine:
  * load `.edb` catalogs (`db_crack_line`), `xephem_sites`, and constellation
    figures (`cns_loadfigs`);
  * fill a libastro `Now` (UT MJD, lat/long, pressure/temperature,
    `n_epoch = EOD`) and call `obj_cir()` for every object to get
    apparent, refracted alt/az;
  * precess constellation figures and IAU boundaries to the epoch of date
    (`cns_figure`, `cns_edges`).
* `skywin.c` is a native Win32/GDI renderer: a stereographic alt/az
  projection (zenith dome or any direction), horizon/ground, star colour
  by spectral class, sky brightness from the Sun's altitude, planets,
  Sun, Moon with phase, hit-testing and an info readout.

## 3. Can you watch the sky at a historical date from a given place?

**Yes.** The original XEphem already supports this. Its manual says
negative years are BC and that there is no year 0. Every libastro
computation takes an arbitrary MJD. NeoXEphem Sky exposes this directly:
you enter a year such as `-500`, a place, and a time, and get the full
sky.

### What the engine accounts for (verified in source)

| Effect | Implementation | Historical behaviour |
|---|---|---|
| Calendar | `cal_mjd()` in `mjd.c` | Julian before 1582-10-15, Gregorian after; BC years with no year 0 |
| Earth-rotation clock (UT vs TT) | `deltat.c` | Morrison & Stephenson (2004) table for -1000…1700 and a long-term parabola before -1000. Applied automatically |
| Precession | `precess.c`, Capitaine/IAU-2006-style polynomials in T | Gives Thuban as the pole star in about 2800 BC (verified) |
| Nutation, aberration, refraction | `nutation.c`, `aberration.c`, `refract.c` | Applied |
| Star proper motion | `circum.c`: linear in RA/Dec from the J2000 catalog | Applied, so star patterns shift (Arcturus moves about 1.3° per 2,000 years) |
| Planets | VSOP87D (Mercury–Neptune) and Chapront-95 for the outer planets in 1689–2247 | Usable over ±4,000 years |
| Moon | Moshier's lunar theory | Source table quotes about 0.5″ peak error from -1369 to +3000 |

### Accuracy limits for the far past

1. **Delta-T uncertainty is the dominant error.** At 500 BC, Delta-T is
   about 17,000 s with roughly ±300–500 s uncertainty. That rotates the
   whole sky against the horizon by up to about 2°. It hardly matters for
   "what was up", but it matters for exact rise times and the timing of
   eclipses and occultations. Delta-T before about 700 BC is extrapolated.
2. **Linear proper motion** is fine for a few thousand years. Beyond about
   ±10,000 years, or for stars near the celestial pole, positions degrade.
   The catalog also has no radial velocities and no perspective
   acceleration.
3. **The precession polynomials** are accurate to arcseconds within a few
   millennia of J2000 and degrade beyond about ±10,000 years. A long-term
   model such as Vondrák 2011 would be needed for prehistoric skies.
4. **Planets and Moon**: VSOP87 is good to arcseconds within ±2,000 years
   and to arcminutes at ±4,000 years. Before about 1400 BC the Moon theory
   is outside its fitted range.
5. **Magnitudes are modern.** Variable stars, novae and supernovae (for
   example SN 1054) are not historical. Messier objects are modern J2000
   positions.
6. **Local time.** Historical dates should use local mean time or apparent
   solar time, not modern zones. The GUI has a one-click LMT button.
7. **Catalog depth**: the sky is drawn to magnitude 6.5 (the naked-eye
   limit), which suits historical naked-eye skies. Deeper XEphem catalogs
   (`XEphem/Catalogs` repo, GSC, UCAC) are optional extras.

### Ideas for a fuller historical-sky product

* Add a long-term precession model (Vondrák 2011) and rigorous space
  motion (Hipparcos/Gaia parallax plus radial velocity) for skies beyond
  ±5,000 years.
* Show the Delta-T uncertainty band, for example rotating the sky by the
  ±1σ range.
* Calendar converters for Vietnamese/Chinese lunisolar, Hebrew and Islamic
  calendars, plus apparent solar time.
* Add bundled historical presets: ancient sites (Babylon, Giza, Thăng Long,
  Angkor), the solar eclipse of 585 BC, SN 1054, Halley apparitions.
* Port further XEphem screens on top of `skycore`: the data table,
  rise/set, the Moon view and the solar system.
