// repro_bundle.h — reproducibility bundle export (Platform 3.0 Phase K).
//
// A bundle is a DIRECTORY (no TB-scale data by default):
//   <outputDir>/
//     manifest.json          — format version, mode, software, environment
//     inputs.json            — input references + checksums
//     workflows.json         — workflow definitions + parameters as run
//     results.json           — result manifests + metrics
//     provenance.json        — lineage edge slice
//     data/                  — only in portable mode (relative layout)
#pragma once

#include <QString>
#include <QStringList>

class QJsonObject;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::workspace
{

class WorkspaceService;

struct ReproBundleOptions
{
    enum class Mode
    {
        ReferenceOnly,   ///< paths only, no copies, no digests
        MetadataOnly,    ///< + checksums/sizes (digest pass over local files)
        Portable,        ///< + copy referenced files into data/ (capped)
    };
    QString outputDir;
    Mode mode = Mode::ReferenceOnly;
    bool includeEnvironment = true;
    qint64 portableMaxBytes = 20LL * 1024 * 1024 * 1024;  ///< portable copy cap
};

struct ReproBundleReport
{
    QString manifestPath;
    int inputCount = 0;
    int workflowCount = 0;
    int resultCount = 0;
    int copiedCount = 0;
    qint64 copiedBytes = 0;
    QStringList warnings;
    bool ok = false;
    QJsonObject toJson() const;
};

class ReproBundleExporter
{
  public:
    ReproBundleExporter( sicnu::data::DataManager &dataManager, WorkspaceService &service );

    ReproBundleReport exportBundle( const ReproBundleOptions &options ) const;

  private:
    sicnu::data::DataManager &m_dataManager;
    WorkspaceService &m_service;
};

} // namespace sicnu::workspace
