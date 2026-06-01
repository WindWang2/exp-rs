// This file forces AUTOMOC to process the QScintilla lexer stub headers.
// The stubs have Q_OBJECT macros but are in src/stubs/Qsci/, outside
// the gui source tree. AUTOMOC only processes headers from the target's
// source directory, so this file includes them to trigger MOC.
#include "../../stubs/Qsci/qscilexer.h"
#include "../../stubs/Qsci/qscilexercss.h"
#include "../../stubs/Qsci/qscilexersql.h"
#include "../../stubs/Qsci/qscilexerpython.h"
#include "../../stubs/Qsci/qsciapis.h"
