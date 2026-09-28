// tests/adversarial_corpus.h — Track 8 R4 WP-G: the shared loader for
// tests/data/harness_adversarial_corpus.json, the single adversarial sample
// source consumed by ≥2 test targets (plan-layer matrix, SSE-lane matrix).
//
// The loader VALIDATES the corpus schema on every load: _schema version
// prefix, a non-empty samples array, and per-sample required fields
// (id / failure_class / target_seam / payload / expected.outcome). A corpus
// that drifts from its own schema fails the load — the schema check is part
// of every consumer's test, not a separate lint step.

#pragma once

#include <json/json.h>

#include <QFile>
#include <QString>

#include <memory>
#include <string>
#include <vector>

namespace sicnu::testing {

struct AdversarialSample
{
    std::string id;
    std::string failureClass;
    std::string targetSeam;
    Json::Value payload;
    Json::Value expected;
};

class AdversarialCorpus
{
  public:
    /// Loads and schema-validates the corpus. Returns false with a reason
    /// when the file is missing, unreadable, or schema-invalid.
    static bool load( const std::string &path, AdversarialCorpus &out, std::string *error )
    {
        QFile file( QString::fromStdString( path ) );
        if ( !file.open( QIODevice::ReadOnly ) )
            return fail( error, "cannot open corpus: " + path );
        const QByteArray bytes = file.readAll();

        Json::Value doc;
        Json::CharReaderBuilder builder;
        std::string parseErrors;
        std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &doc,
                             &parseErrors ) )
            return fail( error, "corpus is not valid JSON: " + parseErrors );

        // — schema self-validation —
        if ( !doc.isObject() )
            return fail( error, "corpus must be an object" );
        static constexpr const char *kSchemaPrefix = "harness_adversarial_corpus/";
        if ( !doc.isMember( "_schema" ) || !doc["_schema"].isString() ||
             doc["_schema"].asString().rfind( kSchemaPrefix, 0 ) != 0 )
            return fail( error, "corpus _schema must start with " + std::string( kSchemaPrefix ) );
        if ( !doc.isMember( "samples" ) || !doc["samples"].isArray() || doc["samples"].empty() )
            return fail( error, "corpus needs a non-empty samples array" );

        std::vector<AdversarialSample> samples;
        for ( const Json::Value &entry : doc["samples"] )
        {
            AdversarialSample sample;
            if ( !entry.isObject() )
                return fail( error, "sample must be an object" );
            sample.id = stringField( entry, "id" );
            sample.failureClass = stringField( entry, "failure_class" );
            sample.targetSeam = stringField( entry, "target_seam" );
            if ( sample.id.empty() || sample.failureClass.empty() || sample.targetSeam.empty() )
                return fail( error, "sample needs id, failure_class, target_seam" );
            if ( !entry.isMember( "payload" ) )
                return fail( error, "sample '" + sample.id + "' needs a payload" );
            sample.payload = entry["payload"];
            if ( !entry.isMember( "expected" ) || !entry["expected"].isObject() ||
                 !entry["expected"].isMember( "outcome" ) )
                return fail( error, "sample '" + sample.id + "' needs expected.outcome" );
            sample.expected = entry["expected"];
            samples.push_back( std::move( sample ) );
        }

        out.mSamples = std::move( samples );
        return true;
    }

    const std::vector<AdversarialSample> &samples() const { return mSamples; }

    /// The samples aimed at one seam (e.g. "agent_plan.read").
    std::vector<AdversarialSample> forSeam( const std::string &seam ) const
    {
        std::vector<AdversarialSample> matched;
        for ( const AdversarialSample &sample : mSamples )
            if ( sample.targetSeam == seam )
                matched.push_back( sample );
        return matched;
    }

  private:
    static bool fail( std::string *error, const std::string &why )
    {
        if ( error )
            *error = why;
        return false;
    }

    static std::string stringField( const Json::Value &value, const char *key )
    {
        return value.isMember( key ) && value[key].isString() ? value[key].asString()
                                                              : std::string();
    }

    std::vector<AdversarialSample> mSamples;
};

/// The corpus path, resolved from THIS header's location in the source
/// tree (__FILE__ is the header's own path including its filename — the
/// filename component must be stripped before "../data" can resolve).
inline std::string adversarialCorpusPath()
{
    const std::string here( __FILE__ );
    const std::string::size_type slash = here.find_last_of( "/\\" );
    const std::string dir = slash == std::string::npos ? std::string() : here.substr( 0, slash );
    return dir + "/data/harness_adversarial_corpus.json";
}

} // namespace sicnu::testing
