#pragma once
#include <QColor>
#include <QIcon>

enum class ReaderIcon {
    Navigate, Highlight, Draw, Text, Eraser, Color, Undo, Save, Outline, Help,
    Light, Dark, Sepia, Burgundy, ThinStroke, MediumStroke, ThickStroke
};

// Original vector artwork, distributed under the project's GPL v2 license.
QIcon readerIcon(ReaderIcon icon, QColor foreground);
