#pragma once
// QwtScaleMap - coordinate transformation between paint and scale coordinates
#include "qwt_global.h"

class QWT_EXPORT QwtScaleMap {
public:
    QwtScaleMap() = default;

    void setPaintInterval(double p1, double p2) { m_p1 = p1; m_p2 = p2; }
    void setScaleInterval(double s1, double s2) { m_s1 = s1; m_s2 = s2; }

    double transform(double v) const {
        if (m_s2 == m_s1) return m_p1;
        return m_p1 + (v - m_s1) * (m_p2 - m_p1) / (m_s2 - m_s1);
    }

    double invTransform(double v) const {
        if (m_p2 == m_p1) return m_s1;
        return m_s1 + (v - m_p1) * (m_s2 - m_s1) / (m_p2 - m_p1);
    }

    double p1() const { return m_p1; }
    double p2() const { return m_p2; }
    double s1() const { return m_s1; }
    double s2() const { return m_s2; }

private:
    double m_p1 = 0.0;
    double m_p2 = 0.0;
    double m_s1 = 0.0;
    double m_s2 = 0.0;
};
