// worker_process_io.h — shared blocking line IO over a worker QProcess
// (Execution Plane 7.0). Factored out of local_worker_host.cpp /
// local_worker_pool.cpp so the one-shot host and the pool share one
// readFrame/writeLine implementation plus the bounded stderr diagnostics
// ring (worker_protocol's stderr = diagnostics channel was never read
// before, so crash diagnostics were lost).
//
// Host-side only: every call blocks the calling thread with QProcess
// waitFor* — the owner-thread discipline of the callers applies unchanged.
#pragma once

#include "runtime/worker/worker_protocol.h"

#include <QByteArray>
#include <QProcess>
#include <QString>

#include <chrono>
#include <string>

namespace sicnu::processing
{

/// Bounded tail of a worker's stderr (diagnostics channel). Keeps the last
/// @p capacity bytes so a crashed/hung worker's last words survive for the
/// typed error report and telemetry without unbounded growth.
class WorkerDiagnosticsRing
{
  public:
    explicit WorkerDiagnosticsRing( int capacityBytes = 32 * 1024 )
        : m_capacity( capacityBytes )
    {
    }

    /// Drains everything currently readable from the process's stderr
    /// channel into the ring (non-blocking).
    void drain( QProcess &process )
    {
        for ( ;; )
        {
            const QByteArray chunk = process.readAllStandardError();
            if ( chunk.isEmpty() )
                break;
            append( chunk );
        }
    }

    void append( const QByteArray &chunk )
    {
        m_buffer.append( chunk );
        if ( m_buffer.size() > m_capacity )
            m_buffer.remove( 0, m_buffer.size() - m_capacity );
    }

    /// Last diagnostics as one bounded single-line string (newlines become
    /// " | " so the text survives inside typed error messages and telemetry
    /// detail fields).
    QString tail() const
    {
        QString text = QString::fromUtf8( m_buffer );
        text.replace( QLatin1Char( '\r' ), QLatin1Char( ' ' ) );
        text.replace( QLatin1Char( '\n' ), QLatin1String( " | " ) );
        while ( text.startsWith( QLatin1Char( ' ' ) ) )
            text.remove( 0, 1 );
        return text;
    }

    bool isEmpty() const { return m_buffer.isEmpty(); }

  private:
    int m_capacity = 0;
    QByteArray m_buffer;
};

inline bool workerWriteLine( QProcess &process, const std::string &line )
{
    const QByteArray bytes = QByteArray::fromStdString( line + "\n" );
    process.write( bytes );
    return process.waitForBytesWritten( 5000 );
}

/// Reads one protocol frame with a hard deadline and a soft (cancellation-
/// poll) deadline. Returns false on EOF/crash (@p workerCrashed set), hard
/// timeout, or soft timeout (@p softTimedOut set — the caller keeps polling).
/// @p diagnostics (optional) is drained from stderr on every poll slice so a
/// chatty worker cannot wedge the host and a dying worker's output is kept.
inline bool workerReadFrame( QProcess &process,
                             std::chrono::steady_clock::time_point deadline,
                             Json::Value &frame, bool &workerCrashed,
                             std::chrono::steady_clock::time_point softDeadline,
                             bool &softTimedOut,
                             WorkerDiagnosticsRing *diagnostics = nullptr,
                             std::string *badFrame = nullptr )
{
    workerCrashed = false;
    while ( true )
    {
        while ( process.canReadLine() )
        {
            const QByteArray raw = process.readLine();
            const std::string line = QString::fromUtf8( raw ).trimmed().toStdString();
            if ( line.empty() )
                continue;
            if ( !sicnu::runtime::worker::parseFrame( line, frame ) )
            {
                // Malformed or version mismatch — a hard refusal, but the
                // caller must be able to DISTINGUISH it from a deadline
                // expiry: report the offending line (bounded) instead of
                // falling through to a misleading "no reply" timeout.
                if ( badFrame )
                    *badFrame = line.substr( 0, 400 );
                return false;
            }
            return true;
        }
        if ( process.state() != QProcess::Running && !process.canReadLine() )
        {
            workerCrashed = true;
            if ( diagnostics )
                diagnostics->drain( process );
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        if ( now >= deadline )
        {
            if ( diagnostics )
                diagnostics->drain( process );
            return false;
        }
        if ( now >= softDeadline )
        {
            softTimedOut = true;
            if ( diagnostics )
                diagnostics->drain( process );
            return false;
        }
        if ( !process.waitForReadyRead( 100 ) )
        {
            if ( diagnostics )
                diagnostics->drain( process );
            if ( process.state() != QProcess::Running && !process.canReadLine() )
            {
                workerCrashed = true;
                return false;
            }
        }
    }
}

} // namespace sicnu::processing
