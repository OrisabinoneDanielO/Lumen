#pragma once

#include <QElapsedTimer>

namespace lumen::render {

// Time for transitions, in milliseconds. Real time normally; tests switch to a
// manual clock and step it, so mid-fade frames are exact on any machine.
// Switching between the two never makes time go backwards.
class RenderClock
{
public:
    RenderClock() { m_real.start(); }

    qint64 now() const { return m_manual ? m_manualMs : m_real.elapsed() + m_offsetMs; }

    bool isManual() const { return m_manual; }

    void setManual(bool manual)
    {
        if (manual == m_manual)
            return;
        if (manual)
            m_manualMs = m_real.elapsed() + m_offsetMs;
        else
            m_offsetMs = m_manualMs - m_real.elapsed();
        m_manual = manual;
    }

    void advance(qint64 ms)
    {
        if (m_manual && ms > 0)
            m_manualMs += ms;
    }

private:
    QElapsedTimer m_real;
    bool m_manual = false;
    qint64 m_manualMs = 0;
    qint64 m_offsetMs = 0;
};

} // namespace lumen::render
