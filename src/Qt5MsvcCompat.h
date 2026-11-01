#pragma once

// Visual Studio 2026 (MSVC 14.50+) removed stdext::make_checked_array_iterator from <iterator>,
// which Qt 5.15.0's qcompilerdetection.h maps QT_MAKE_CHECKED_ARRAY_ITERATOR to on MSVC.
#include <QtCore/qglobal.h>

#undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)

#undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)
