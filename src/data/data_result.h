#pragma once

#include <optional>
#include <utility>

#include <QString>
#include <QVector>

namespace sicnu::data
{

enum class DiagnosticSeverity
{
  Info,
  Warning,
  Error,
};

struct Diagnostic
{
  QString code;
  QString message;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
};

template <typename T>
class Result
{
  public:
    static Result success( T value, QVector<Diagnostic> diagnostics = {} )
    {
      return Result( std::move( value ), std::move( diagnostics ) );
    }

    static Result failure( Diagnostic diagnostic )
    {
      return Result( std::nullopt, QVector<Diagnostic>{ std::move( diagnostic ) } );
    }

    static Result failure( QVector<Diagnostic> diagnostics )
    {
      return Result( std::nullopt, std::move( diagnostics ) );
    }

    explicit operator bool() const
    {
      return m_value.has_value();
    }

    const T &value() const
    {
      return m_value.value();
    }

    const QVector<Diagnostic> &diagnostics() const
    {
      return m_diagnostics;
    }

  private:
    Result( T value, QVector<Diagnostic> diagnostics )
      : m_value( std::move( value ) )
      , m_diagnostics( std::move( diagnostics ) )
    {
    }

    Result( std::nullopt_t, QVector<Diagnostic> diagnostics )
      : m_diagnostics( std::move( diagnostics ) )
    {
    }

    std::optional<T> m_value;
    QVector<Diagnostic> m_diagnostics;
};

} // namespace sicnu::data
