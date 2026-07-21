#ifndef RS_TEMPLATE_MATCH_DIALOG_H
#define RS_TEMPLATE_MATCH_DIALOG_H

#include <QDialog>

#include "rs_template_matcher.h"

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;

/**
 * Parameters for geo-initialized template (NCC) matching.
 */
class RsTemplateMatchDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit RsTemplateMatchDialog( QWidget *parent = nullptr );

    RsTemplateMatcher::Params params() const;

  private:
    QComboBox *m_seedMode = nullptr;
    QSpinBox *m_templateSize = nullptr;
    QSpinBox *m_searchRadius = nullptr;
    QDoubleSpinBox *m_minScore = nullptr;
    QSpinBox *m_gridRows = nullptr;
    QSpinBox *m_gridCols = nullptr;
};

#endif
