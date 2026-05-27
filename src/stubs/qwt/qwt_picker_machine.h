#pragma once
// ANTIGRAVITY: stub
#include "qwt_global.h"
class QWT_EXPORT QwtPickerMachine {
public:
    virtual ~QwtPickerMachine() {}
};
class QWT_EXPORT QwtPickerClickPointMachine : public QwtPickerMachine {};
class QWT_EXPORT QwtPickerDragPointMachine : public QwtPickerMachine {};
class QWT_EXPORT QwtPickerClickRectMachine : public QwtPickerMachine {};
class QWT_EXPORT QwtPickerDragRectMachine : public QwtPickerMachine {};
