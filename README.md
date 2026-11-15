# Vireo

A native PDF reader in C++17, Qt 6 and Poppler, targeting Linux and FreeBSD.
English is the default interface language; Brazilian Portuguese is included.

## Build

Dependencies: C++17 compiler, CMake ≥ 3.20, Qt ≥ 6.2 (Widgets and Test),
Poppler's Qt 6 bindings, and pkg-config. Qt LinguistTools is optional and builds
translation catalogs. An English-only build works without it.

Debian / Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev libpoppler-qt6-dev qt6-tools-dev qt6-l10n-tools
```

FreeBSD:

```sh
sudo pkg install cmake ninja pkgconf qt6-base qt6-tools poppler-qt6
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/vireo /path/to/document.pdf
```

For a build without tests, use `-DBUILD_TESTING=OFF`. Installation is supported
with `cmake --install build --prefix /your/prefix`.
FreeBSD is a portability target; this initial version has been compiled and tested
on Linux only. No Linux-specific application APIs are used.

## Reading and navigation

| Key | Action |
| --- | --- |
| `h`, `j`, `k`, `l` | Scroll left/down/up/right; `j`/`k` turn pages at the edge |
| `J`, `K` | Next / previous PDF page, starting at the top |
| `H`, `L` | Previous / next chapter start |
| `d`, `u` | Scroll half a viewport down/up |
| `gg`, `G` | First page / end of last page |
| `:123`, `:#123`, `:p 123`, or `:page 123`, then Enter | Go to page 123 (PDF page number, starting at 1) |
| `:c` or `:chapter`, then Enter | List chapters; choose with arrow keys and Enter, or cancel with Esc |
| `:c 3` or `:chapter 3`, then Enter | Go to the third chapter |
| `:c next` / `:c prev` (also `:chapter`) | Next / previous chapter start |
| `Tab` | Toggle the document outline |
| `a` | Toggle the annotation toolbar; hiding it returns to navigation |
| `Esc` | Cancel the page command or return to navigation |
| `+`, `-` | Zoom (50–300%) |
| `Ctrl+O` | Open PDF |
| `Ctrl+S` | Retry saving annotations |
| `Ctrl+Z` | Undo the last annotation addition or deletion in this session |
| `?` | Show help |

The first `g` waits up to 800 ms for the second. Keyboard commands are inactive
while entering text or interacting with a modal dialog. The outline button also
opens/closes the sidebar; PDFs without an outline show an explicit empty state.
Chapters are the top-level outline entries with valid destinations in this PDF.
A heading with no destination uses its first section with a valid local destination.
Subsections remain accessible in the full outline. Chapter numbers follow outline
order; `H`/`L` jump to the closest earlier/later chapter start by page number,
skipping duplicate starts. From inside a chapter, `H` returns to that chapter's
start; press it again to reach the preceding one. Navigation stops at document
boundaries. PDFs without navigable chapters report that state without changing
pages. Page aliases also accept `:p #123` and `:page #123`.

One page is rendered at a time. Rasterization is limited to 16 million pixels per
page to bound memory use. Rendering currently runs on the UI thread.

Choose **Light**, **Dark**, **Sepia**, or **Burgundy** in the reading toolbar.
By default, filters apply to the entire PDF page, including photos and figures,
mapping luminance to the selected paper/ink colors.
Annotations are painted afterwards and keep their selected colors.

## Annotations and original-file preservation

Select a tool in the annotation toolbar:

- **Highlight:** drag a translucent rectangle over an area. This is an area
  highlight, not text-selection-aware highlighting.
- **Draw:** drag a freehand stroke; choose a color and stroke thickness.
- **Text:** drag a box, then enter multiline text and confirm. Text wraps and is
  clipped to the box. Text size scales with the page; text boxes cannot yet be
  edited or resized after creation (erase and recreate them).
- **Eraser:** click or drag over an annotation to delete it. Erasing removes an
  entire stroke, highlight, or text box; overlapping marks are erased topmost
  first. It never erases the original document's text or drawings.
- **Navigate:** stop annotating. `Esc` also selects this tool.

Annotations autosave to `<document.pdf>.vireo.json`, alongside the PDF. The
original PDF is only read, never rewritten. Keep the PDF and sidecar together
when copying them elsewhere. The directory must be writable to save annotations.
If saving fails, changes stay in memory, the status bar reports the error, and
closing or opening another PDF requires an explicit discard decision.

The versioned JSON stores normalized page coordinates, colors, widths, text and a
SHA-256 fingerprint of the PDF. Saving uses `QSaveFile` for atomic replacement.
Invalid sidecars or fingerprints that do not match disable annotation editing,
so existing notes are not overwritten. Password-protected PDFs are not supported
in this version. Simultaneous editing of one sidecar by multiple instances is not
supported. Undo history is session-local and does not persist across reopening.

## Translations

```sh
./build/vireo --language en document.pdf
./build/vireo --language pt_BR document.pdf
```

Without an override, the system locale is used, falling back to English. Qt's
standard dialog translations are used when installed. With Qt LinguistTools,
CMake compiles `translations/vireo_pt_BR.ts` to `build/translations/` and installs
it under `share/vireo/translations`. Add another Qt `.ts` catalog and its build
rule to support another language. All application interface strings use Qt
translation contexts (`ReaderWindow`, `Document`, `AnnotationStore`). CLI help
is currently English.

## Code organization and validation

- `Document`: read-only Poppler loading, fingerprinting, rendering and outline.
- `AnnotationStore`: versioned sidecar parsing, validation and atomic saving.
- `PageCanvas`: page filters, annotation painting, mouse gestures and eraser hit tests.
- `ReaderWindow`: controls, vi commands, outline, annotation history and save errors.
- `main.cpp`: application startup, command-line options and translation loading.

Qt Test exercises document rendering, nested outline navigation, vi commands,
sidecar round trips and rejection, all reading filters, text entry, erasing,
undo, zoom-independent hit tests, translation loading, and byte-for-byte
preservation of the original PDF. Tests run offscreen and need no desktop.

License: GPL-2.0, as provided in `LICENSE`.
