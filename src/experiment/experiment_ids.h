// experiment_ids.h — strong identities for the experiment foundation
// (ADR 0137). RunId must stay filename-safe (it names checkpoint/bundle
// directories); the rest are UUID identities like the dataset layer's.
#pragma once

#include <QString>

#include <optional>

namespace sicnu::experiment
{

#define SICNU_EXPERIMENT_ID_TYPE( Name )                                                      \
    class Name                                                                                \
    {                                                                                         \
      public:                                                                                 \
        Name() = default;                                                                     \
        static Name generate();                                                               \
        static std::optional<Name> fromString( const QString &text );                          \
        bool isNull() const;                                                                   \
        QString toString() const;                                                              \
        friend bool operator==( const Name &, const Name & ) = default;                        \
                                                                                              \
      private:                                                                                \
        explicit Name( QString value );                                                        \
        QString m_value;                                                                       \
    };

SICNU_EXPERIMENT_ID_TYPE( ExperimentId )

#undef SICNU_EXPERIMENT_ID_TYPE

/// RunId: filename-safe, at most 64 chars, [A-Za-z0-9._-], no leading dot.
/// Unlike the UUID ids a run id MAY be human-chosen (it labels bundle
/// directories); validity is still strict — invalid run ids are refused,
/// never sanitized.
class RunId
{
  public:
    RunId() = default;

    static RunId generate();
    static std::optional<RunId> fromString( const QString &text );

    bool isNull() const { return m_value.isEmpty(); }
    const QString &toString() const { return m_value; }

    friend bool operator==( const RunId &, const RunId & ) = default;

  private:
    explicit RunId( QString value );
    QString m_value;
};

} // namespace sicnu::experiment
