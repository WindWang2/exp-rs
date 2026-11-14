#pragma once

#include <functional>
#include <string>
#include <utility>

#include <json/json.h>

namespace sicnu::operators::python
{

/// Host-owned, hash-locked Python environments for agent script tasks.
/// probe() never prepares or installs an environment. prepare() is an explicit
/// operation; execution only consumes an environment whose real imports match
/// the bundled profile and whose completion marker matches its lock.
class PythonEnvironmentManager
{
  public:
    /// The host may supply its executable directory (e.g. an embedding host or
    /// an installation-layout test). Empty uses QCoreApplication's directory;
    /// this location is never accepted from an agent tool request.
    explicit PythonEnvironmentManager( std::string applicationDirectory = {} )
      : m_applicationDirectory( std::move( applicationDirectory ) ) {}

    Json::Value probe( const std::string &profileId,
                       const std::function<bool()> &cancelled = {} ) const;
    Json::Value prepare( const std::string &profileId,
                         const std::function<bool()> &cancelled = {},
                         int timeoutSeconds = 600 ) const;

  private:
    std::string m_applicationDirectory;
};

} // namespace sicnu::operators::python
