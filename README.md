# Nightreader

A desktop PDF reader with vi-style navigation, annotations, and night reading modes.

- Scroll continuously through pages and navigate chapters from the keyboard.
- Read in light, dark, sepia, or burgundy mode.
- Add highlights, freehand drawings, and text boxes in your choice of color.
- Browse the document outline in a collapsible sidebar.
- Save annotations separately, keeping the original PDF untouched.

## Build and run

Requires a C++17 compiler, CMake 3.20 or later, Qt 6.2 or later, Poppler's Qt 6
bindings, and pkg-config.

On Debian / Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev libpoppler-qt6-dev qt6-tools-dev qt6-l10n-tools
```

On FreeBSD:

```sh
sudo pkg install cmake ninja pkgconf qt6-base qt6-tools poppler-qt6
```

From the repository directory:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/nightreader document.pdf
```

Builds are tested on Linux. FreeBSD builds have not been verified yet.

To install, run `cmake --install build --prefix /your/install/path`.

## Keyboard navigation

| Key | Action |
| --- | --- |
| `h`, `j`, `k`, `l` | Scroll left, down, up, right |
| `J`, `K` | Next / previous page |
| `H`, `L` | Previous / next chapter start |
| `d`, `u` | Scroll half a screen down / up |
| `gg`, `G` | First page / end of last page |
| `Tab` | Show or hide the document outline |
| Right-click or `a` | Open annotation tools |
| `t` | Cycle through reading modes |
| `Esc` | Cancel a command or return to navigation |
| `+`, `-` | Zoom in / out |
| `Ctrl+O` | Open a PDF |
| `Ctrl+S` | Save annotations |
| `Ctrl+Z` | Undo an annotation change |
| `?` | Show help |

Pages appear one below the other, so you can see the end of one and the beginning
of the next together. Use the mouse wheel or `j`/`k` to scroll continuously.
`J` and `K` jump to the next or previous page.

Chapters follow the first level of the document outline. From within a chapter,
`H` returns to its beginning; press it again to reach the preceding chapter.
Subsections are accessible through the sidebar. Chapter navigation requires a
PDF with an outline.

### Commands

Type `:` to enter a command, then press Enter to run it or Esc to cancel.
Page and chapter numbers start at 1.

| Command | Action |
| --- | --- |
| `:o`, `:open` | Open the file picker |
| `:o document.pdf`, `:open document.pdf` | Open a PDF by path |
| `:q`, `:quit` | Close the reader |
| `:h`, `:help` | Show help |
| `:t`, `:theme` | Cycle through reading modes |
| `:t dark`, `:theme dark` | Choose a reading mode |
| `:12`, `:#12`, `:p 12`, `:page 12` | Go to page 12 |
| `:c`, `:chapter` | Open the chapter list |
| `:c 3`, `:chapter 3` | Go to chapter 3 |
| `:c next`, `:chapter next` | Go to the next chapter |
| `:c prev`, `:chapter prev` | Go to the previous chapter start |

Paths containing spaces can be quoted: `:open "my documents/book.pdf"`.

In the chapter list, use the arrow keys to choose a chapter and Enter to open it.
Esc closes the list.

## Reading modes

Press `t` to cycle through **Light**, **Dark**, **Sepia**, and **Burgundy**.
To choose directly, use `:theme light`, `:theme dark`, `:theme sepia`, or
`:theme burgundy`. Reading modes are also available in the right-click toolbox.

The filter applies to the whole page, including photos and figures. Annotations
keep their selected colors.

## Annotations

Right-click on a page to open the floating toolbox. Its buttons use icons; hover
over one to see its name. Choose a tool, color, and stroke thickness, then return
to the document to annotate. You can also open the toolbox with `a`.

- **Highlight:** drag a rectangle over the area to highlight.
- **Draw:** draw with the mouse and choose a stroke thickness.
- **Text:** drag a box, enter your text, and confirm. Text wraps within the box.
- **Eraser:** click or drag over a highlight, drawing, or text box to remove it.
- **Navigate:** return to reading. You can also press Esc.

The eraser removes an entire annotation at a time. Use Ctrl+Z to undo an addition
or removal. Text boxes cannot yet be edited or resized; erase and recreate them
to make changes. Undo history resets when you close the document.

Annotations save automatically to `document.pdf.nightreader.json` beside the PDF.
Keep both files together when moving or sharing your notes. You need write access
to that directory. If saving fails, the status bar shows the error; Ctrl+S retries.
Nightreader warns before you leave a document with unsaved changes.

## Interface language

The interface follows your system language, with English as the fallback.
English and Brazilian Portuguese are available. To choose a language explicitly:

```sh
./build/nightreader --language en document.pdf
./build/nightreader --language pt_BR document.pdf
```

Translation catalogs are built when Qt LinguistTools is installed. Without it,
the interface is available in English.

## Current limitations

- Password-protected PDFs are not supported.
- Highlights cover rectangular areas rather than following selected text.
- Large or complex pages may pause the interface while rendering.
- Editing the same annotation file in multiple windows is not supported.

## Development

Nightreader uses C++17, Qt 6, and Poppler. To run the tests after building:

```sh
ctest --test-dir build --output-on-failure
```

Tests run without a desktop session. To build without tests, configure CMake
with `-DBUILD_TESTING=OFF`.

## License

[GNU GPL v2](LICENSE).
