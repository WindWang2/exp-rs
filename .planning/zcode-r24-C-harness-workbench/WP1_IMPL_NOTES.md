# WP1 implementation patch (apply ONLY after the RED run fails)

File: src/agent/workspace_state.cpp

1. Include (after `#include "data/data_manager.h"`):
```cpp
#include "cartography/chart_registry.h"
```

2. Constant (next to kMaxModels):
```cpp
constexpr int kMaxCharts = 25;
```

3. Replace the charts block:
```cpp
  // --- charts (Phase L registry; empty until charts are created) -----------
  Json::Value charts( Json::arrayValue );
  body["charts"] = charts;
```
with:
```cpp
  // --- charts (live ChartRegistry — the registry cartography:chart_* mutates)
  // The tool surface always promised charts in this document (see the
  // workspace_state description in tests/surface_diff_snapshot.json); the
  // registry is the single authority, read under its own mutex. Bounded at
  // the summary cap chart_registry.h declares (25); binding data is not
  // projected — agents fetch it via cartography:chart_get.
  Json::Value charts( Json::arrayValue );
  {
    const Json::Value all =
      cartography::ChartRegistry::instance().listCharts();
    int chartCount = 0;
    for ( const Json::Value &chart : all )
    {
      if ( chartCount++ >= kMaxCharts )
        break;
      Json::Value c( Json::objectValue );
      const QString chartId = QString::fromStdString( chart["id"].asString() );
      c["id"] = entities.idFor( QStringLiteral( "chart" ), chartId ).toStdString();
      c["chart_id"] = chartId.toStdString();
      c["kind"] = chart["kind"].asString();
      c["title"] = chart["title"].asString();
      charts.append( c );
    }
  }
  body["charts"] = charts;
```

Note: file is inside namespace sicnu::agent → `cartography::ChartRegistry` resolves. QSet/QMap refs fine. Entity kind "chart" persists like others (workspace_state.cpp:104,137-138 pattern).
