/***************************************************************************
  agent/spatial_tools/temporal_spatial_tools.h
  Temporal Phenology Timeline Studio (D16) — agent-facing temporal tools.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Function-calling tools for LLM agents (OpenAI / Anthropic schema shape):

    temporal:phenology_query  — SOS / POS / EOS for a pixel-year
    temporal:trend_inspect    — Theil-Sen + Mann-Kendall over a series
    temporal:anomaly_alert    — standardized monthly anomaly (z-score)
                                drought screen: >= 3 consecutive months at
                                z <= -1.5 raise the alert

  Data path (DECISIONS D-160-9): tools execute against an in-memory series
  catalog keyed by (metric, lon, lat) at 0.25-degree quantization; tests and
  the workbench ingest series explicitly. Every response is structured:
  { "status": "success", ... } or { "status": "rejected", "reason": ... } —
  a tool NEVER returns hallucinated numbers for invalid inputs (out-of-range
  coordinates, missing series, phenology order violations).
 ***************************************************************************/

#ifndef SICNU_AGENT_SPATIAL_TOOLS_TEMPORAL_SPATIAL_TOOLS_H
#define SICNU_AGENT_SPATIAL_TOOLS_TEMPORAL_SPATIAL_TOOLS_H

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::agent
{

class TemporalSpatialTool
{
  public:
    /// OpenAI/Anthropic function-calling registration schema for the three
    /// temporal tools.
    static Json::Value toolSchema();

    /// Executes one tool call; unknown names are rejected, never guessed.
    static Json::Value executeTool( const std::string &toolName, const Json::Value &arguments );

    /// Ingests one monthly series (12 values per year, starting at
    /// @a startYear) for the quantized (metric, lon, lat) cell.
    static void ingestSeries( const std::string &metric, double lon, double lat, int startYear,
                              const std::vector<float> &monthlyValues );
    static void clearSeries();

  private:
    struct SeriesKey
    {
        std::string metric;
        int lonQ = 0; // quantized 0.25-degree cell
        int latQ = 0;
        bool operator<( const SeriesKey &other ) const
        {
            if ( metric != other.metric )
                return metric < other.metric;
            if ( lonQ != other.lonQ )
                return lonQ < other.lonQ;
            return latQ < other.latQ;
        }
    };
    struct SeriesRecord
    {
        int startYear = 0;
        std::vector<float> monthly; // 12 · years values
    };
    static std::map<SeriesKey, SeriesRecord> &catalog();
    static SeriesRecord *findSeries( const std::string &metric, double lon, double lat );
};

} // namespace sicnu::agent

#endif // SICNU_AGENT_SPATIAL_TOOLS_TEMPORAL_SPATIAL_TOOLS_H
