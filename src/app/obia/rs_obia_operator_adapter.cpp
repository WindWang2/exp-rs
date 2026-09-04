// rs_obia_operator_adapter.cpp — see rs_obia_operator_adapter.h
#include "rs_obia_operator_adapter.h"

// rs_object_hierarchy.h (data contract, included via the adapter header)
// brings in RsParentTable — no kernel headers needed for rehydration.
#include "rs_segment_map.h"

#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTextStream>

namespace RsObiaOperatorAdapter
{

namespace
{

std::string toStd( const QString &s ) { return s.toStdString(); }

/// #rrggbb for the operators' classColors contract.
QString colorString( const QColor &c )
{
    return QStringLiteral( "#%1%2%3" )
        .arg( c.red(), 2, 16, QChar( '0' ) )
        .arg( c.green(), 2, 16, QChar( '0' ) )
        .arg( c.blue(), 2, 16, QChar( '0' ) );
}

QStringList readCsvLines( const QString &path, QString *error )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
    {
        if ( error )
            *error = QObject::tr( "Cannot open %1" ).arg( path );
        return {};
    }
    QStringList lines;
    QTextStream in( &f );
    while ( !in.atEnd() )
    {
        const QString line = in.readLine().trimmed();
        if ( !line.isEmpty() )
            lines.append( line );
    }
    return lines;
}

} // namespace

Json::Value buildSegmentParams( const SegmentOptions &opts )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( opts.rasterPath );
    params["output"] = toStd( opts.outputLabelsPath );
    params["engine"] = toStd( opts.engine );
    params["smoothKernel"] = opts.smoothKernel;
    params["quantizeBins"] = opts.quantizeBins;
    params["minRegionSize"] = opts.minRegionSize;
    params["spatialRadius"] = opts.spatialRadius;
    params["rangeRadius"] = opts.rangeRadius;
    params["maxIterations"] = opts.maxIterations;
    params["threshold"] = opts.threshold;
    return params;
}

Json::Value buildFeaturesParams( const QString &rasterPath,
                                 const QString &labelsPath,
                                 const QString &outputCsvPath )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( rasterPath );
    params["labels"] = toStd( labelsPath );
    params["output"] = toStd( outputCsvPath );
    return params;
}

Json::Value buildLabelParams( const QString &rasterPath,
                              const QString &labelsPath,
                              const QString &trainingPath,
                              const QString &classField,
                              int minLabelPixels )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( rasterPath );
    params["labels"] = toStd( labelsPath );
    params["training"] = toStd( trainingPath );
    if ( !classField.isEmpty() )
        params["classField"] = toStd( classField );
    params["minLabelPixels"] = minLabelPixels;
    return params;
}

Json::Value segmentClassesJson( const QMap<quint32, int> &segmentClasses )
{
    Json::Value json( Json::objectValue );
    for ( auto it = segmentClasses.constBegin(); it != segmentClasses.constEnd(); ++it )
        json[std::to_string( it.key() )] = it.value();
    return json;
}

Json::Value classColorsJson( const QHash<int, QColor> &classColors )
{
    Json::Value json( Json::objectValue );
    for ( auto it = classColors.constBegin(); it != classColors.constEnd(); ++it )
        json[std::to_string( it.key() )] = toStd( colorString( it.value() ) );
    return json;
}

Json::Value featureSelectionJson( const RsFeatureSelection &selection )
{
    Json::Value json( Json::objectValue );
    json["mean"] = selection.useMean;
    json["stddev"] = selection.useStdDev;
    json["min"] = selection.useMin;
    json["max"] = selection.useMax;
    json["glcmContrast"] = selection.useGlcmContrast;
    json["glcmCorrelation"] = selection.useGlcmCorrelation;
    json["glcmEnergy"] = selection.useGlcmEnergy;
    json["glcmHomogeneity"] = selection.useGlcmHomogeneity;
    json["area"] = selection.useArea;
    json["perimeter"] = selection.usePerimeter;
    json["shapeIndex"] = selection.useShapeIndex;
    json["compactness"] = selection.useCompactness;
    json["rectangularity"] = selection.useRectangularity;
    json["aspectRatio"] = selection.useAspectRatio;
    return json;
}

Json::Value buildFlatClassifyParams( const QString &rasterPath,
                                     const QString &labelsPath,
                                     const QString &outputPath,
                                     const QMap<quint32, int> &segmentClasses,
                                     const ClassifierOptions &classifier,
                                     const RsFeatureSelection &featureSelection,
                                     const QHash<int, QColor> &classColors,
                                     const QString &outputUncertaintyPath )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( rasterPath );
    params["labels"] = toStd( labelsPath );
    params["output"] = toStd( outputPath );
    params["segmentClasses"] = segmentClassesJson( segmentClasses );
    params["method"] = toStd( classifier.method );
    params["scale"] = classifier.scale;
    params["features"] = "full";
    params["featureSelection"] = featureSelectionJson( featureSelection );
    params["rfNumTrees"] = classifier.rfNumTrees;
    params["rfMaxDepth"] = classifier.rfMaxDepth;
    params["rfMinSampleCount"] = classifier.rfMinSampleCount;
    params["mlpHiddenLayerSize"] = classifier.mlpHiddenLayerSize;
    params["mlpMaxIter"] = classifier.mlpMaxIter;
    if ( !classColors.isEmpty() )
        params["classColors"] = classColorsJson( classColors );
    if ( !outputUncertaintyPath.isEmpty() )
        params["outputUncertainty"] = toStd( outputUncertaintyPath );
    return params;
}

Json::Value buildHierarchyBuildParams( const QString &rasterPath,
                                       const QString &outputFinePath,
                                       const QString &outputCoarsePath,
                                       const QString &outputParentsPath,
                                       int spatialRadius,
                                       double rangeRadius,
                                       int minRegionSize,
                                       double watershedThreshold )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( rasterPath );
    params["outputFine"] = toStd( outputFinePath );
    if ( !outputCoarsePath.isEmpty() )
        params["outputCoarse"] = toStd( outputCoarsePath );
    if ( !outputParentsPath.isEmpty() )
        params["outputParents"] = toStd( outputParentsPath );
    params["spatialRadius"] = spatialRadius;
    params["rangeRadius"] = rangeRadius;
    params["minRegionSize"] = minRegionSize;
    params["watershedThreshold"] = watershedThreshold;
    return params;
}

Json::Value buildHierarchyClassifyParams( const QString &rasterPath,
                                          const QString &labelsFinePath,
                                          const QString &labelsCoarsePath,
                                          const QString &parentsPath,
                                          const QString &outputPath,
                                          int classifyLevel,
                                          const QMap<quint32, int> &segmentClasses,
                                          const ClassifierOptions &classifier,
                                          const QHash<int, QColor> &classColors,
                                          const QString &outputUncertaintyPath )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( rasterPath );
    params["labelsFine"] = toStd( labelsFinePath );
    if ( !labelsCoarsePath.isEmpty() )
        params["labelsCoarse"] = toStd( labelsCoarsePath );
    if ( !parentsPath.isEmpty() )
        params["parents"] = toStd( parentsPath );
    params["outputClass"] = toStd( outputPath );
    params["classifyLevel"] = classifyLevel;
    params["segmentClasses"] = segmentClassesJson( segmentClasses );
    params["method"] = toStd( classifier.method );
    params["rfNumTrees"] = classifier.rfNumTrees;
    params["rfMaxDepth"] = classifier.rfMaxDepth;
    params["rfMinSampleCount"] = classifier.rfMinSampleCount;
    params["mlpHiddenLayerSize"] = classifier.mlpHiddenLayerSize;
    params["mlpMaxIter"] = classifier.mlpMaxIter;
    if ( !classColors.isEmpty() )
        params["classColors"] = classColorsJson( classColors );
    if ( !outputUncertaintyPath.isEmpty() )
        params["outputUncertainty"] = toStd( outputUncertaintyPath );
    return params;
}

Json::Value buildPolygonizeParams( const QString &classRasterPath,
                                   const QString &outputVectorPath )
{
    Json::Value params( Json::objectValue );
    params["input"] = toStd( classRasterPath );
    params["output"] = toStd( outputVectorPath );
    params["field"] = "class_id";
    params["connected8"] = true;
    return params;
}

QString methodForClassifierLabel( const QString &label )
{
    if ( label.contains( QStringLiteral( "NormalBayes" ), Qt::CaseInsensitive )
         || label.contains( QStringLiteral( "bayes" ), Qt::CaseInsensitive ) )
        return QStringLiteral( "normal_bayes" );
    if ( label.contains( QStringLiteral( "RandomForest" ), Qt::CaseInsensitive )
         || label.contains( QStringLiteral( "forest" ), Qt::CaseInsensitive ) )
        return QStringLiteral( "random_forest" );
    if ( label.contains( QStringLiteral( "KMeans" ), Qt::CaseInsensitive ) )
        return QStringLiteral( "kmeans" );
    if ( label.contains( QStringLiteral( "MLP" ), Qt::CaseInsensitive )
         || label.contains( QStringLiteral( "Neural" ), Qt::CaseInsensitive ) )
        return QStringLiteral( "mlp" );
    return QStringLiteral( "svm" );
}

bool parseFeaturesCsv( const QString &csvPath,
                       QMap<quint32, RsSegmentFeatures::SegmentStat> &stats,
                       QString *error )
{
    stats.clear();
    const QStringList lines = readCsvLines( csvPath, error );
    if ( lines.isEmpty() )
    {
        if ( error && error->isEmpty() )
            *error = QObject::tr( "Empty features CSV: %1" ).arg( csvPath );
        return false;
    }

    const QStringList header = lines.first().split( ',' );
    const int idCol = header.indexOf( QStringLiteral( "segment_id" ) );
    if ( idCol < 0 )
    {
        if ( error )
            *error = QObject::tr( "Features CSV has no segment_id column" );
        return false;
    }

    // Column name → value slot per segment, resolved once from the header.
    const auto colIndex = [&header]( const QString &name ) { return header.indexOf( name ); };
    struct ColumnMap
    {
        int area = -1, perimeter = -1, shapeIndex = -1, compactness = -1,
            rectangularity = -1, aspectRatio = -1;
        QVector<int> mean, stddev, min, max, glcmContrast, glcmCorrelation, glcmEnergy, glcmHomogeneity;
    } cols;
    cols.area = colIndex( QStringLiteral( "area" ) );
    cols.perimeter = colIndex( QStringLiteral( "perimeter" ) );
    cols.shapeIndex = colIndex( QStringLiteral( "shape_index" ) );
    cols.compactness = colIndex( QStringLiteral( "compactness" ) );
    cols.rectangularity = colIndex( QStringLiteral( "rectangularity" ) );
    cols.aspectRatio = colIndex( QStringLiteral( "aspect_ratio" ) );
    int bands = 0;
    while ( colIndex( QStringLiteral( "mean_b%1" ).arg( bands + 1 ) ) >= 0 )
        ++bands;
    if ( bands <= 0 )
    {
        if ( error )
            *error = QObject::tr( "Features CSV has no per-band columns" );
        return false;
    }
    for ( int b = 1; b <= bands; ++b )
    {
        const QString suffix = QStringLiteral( "_b%1" ).arg( b );
        cols.mean.append( colIndex( QStringLiteral( "mean" ) + suffix ) );
        cols.stddev.append( colIndex( QStringLiteral( "stddev" ) + suffix ) );
        cols.min.append( colIndex( QStringLiteral( "min" ) + suffix ) );
        cols.max.append( colIndex( QStringLiteral( "max" ) + suffix ) );
        cols.glcmContrast.append( colIndex( QStringLiteral( "glcm_contrast" ) + suffix ) );
        cols.glcmCorrelation.append( colIndex( QStringLiteral( "glcm_correlation" ) + suffix ) );
        cols.glcmEnergy.append( colIndex( QStringLiteral( "glcm_energy" ) + suffix ) );
        cols.glcmHomogeneity.append( colIndex( QStringLiteral( "glcm_homogeneity" ) + suffix ) );
    }

    const auto valueAt = []( const QStringList &row, int col, double defaultValue ) {
        if ( col < 0 || col >= row.size() )
            return defaultValue;
        return row.at( col ).toDouble();
    };
    const auto vectorAt = [&]( const QStringList &row, const QVector<int> &colsB ) {
        QVector<double> v( colsB.size() );
        for ( int b = 0; b < colsB.size(); ++b )
            v[b] = valueAt( row, colsB[b], 0.0 );
        return v;
    };

    for ( int i = 1; i < lines.size(); ++i )
    {
        const QStringList row = lines.at( i ).split( ',' );
        if ( row.size() < header.size() )
        {
            if ( error )
                *error = QObject::tr( "Malformed features CSV row %1" ).arg( i + 1 );
            return false;
        }
        const quint32 segId = row.at( idCol ).toUInt();
        if ( segId == 0 )
            continue;
        RsSegmentFeatures::SegmentStat stat;
        stat.area = valueAt( row, cols.area, 0.0 );
        stat.perimeter = valueAt( row, cols.perimeter, 0.0 );
        stat.shapeIndex = valueAt( row, cols.shapeIndex, 0.0 );
        stat.compactness = valueAt( row, cols.compactness, 0.0 );
        stat.rectangularity = valueAt( row, cols.rectangularity, 0.0 );
        stat.aspectRatio = valueAt( row, cols.aspectRatio, 0.0 );
        stat.mean = vectorAt( row, cols.mean );
        stat.stddev = vectorAt( row, cols.stddev );
        stat.min = vectorAt( row, cols.min );
        stat.max = vectorAt( row, cols.max );
        stat.glcmContrast = vectorAt( row, cols.glcmContrast );
        stat.glcmCorrelation = vectorAt( row, cols.glcmCorrelation );
        stat.glcmEnergy = vectorAt( row, cols.glcmEnergy );
        stat.glcmHomogeneity = vectorAt( row, cols.glcmHomogeneity );
        stats.insert( segId, stat );
    }
    if ( stats.isEmpty() && error )
        *error = QObject::tr( "Features CSV contains no data rows" );
    return !stats.isEmpty();
}

bool parseSegmentClassesCsv( const QString &csvPath,
                             QMap<quint32, int> &segmentClasses,
                             QString *error )
{
    segmentClasses.clear();
    const QStringList lines = readCsvLines( csvPath, error );
    if ( lines.isEmpty() )
    {
        if ( error && error->isEmpty() )
            *error = QObject::tr( "Empty label CSV: %1" ).arg( csvPath );
        return false;
    }
    const QStringList header = lines.first().split( ',' );
    const int idCol = header.indexOf( QStringLiteral( "segment_id" ) );
    const int classCol = header.indexOf( QStringLiteral( "class_id" ) );
    if ( idCol < 0 || classCol < 0 )
    {
        if ( error )
            *error = QObject::tr( "Label CSV must have segment_id,class_id columns" );
        return false;
    }
    for ( int i = 1; i < lines.size(); ++i )
    {
        const QStringList row = lines.at( i ).split( ',' );
        if ( row.size() <= ( std::max )( idCol, classCol ) )
            continue;
        const quint32 segId = row.at( idCol ).toUInt();
        const int classId = row.at( classCol ).toInt();
        if ( segId != 0 && classId > 0 )
            segmentClasses.insert( segId, classId );
    }
    return true;
}

bool parseUncertaintyCsv( const QString &csvPath,
                          QMap<quint32, double> &uncertainties,
                          QMap<quint32, int> &predictedClasses,
                          QString *error )
{
    uncertainties.clear();
    predictedClasses.clear();
    const QStringList lines = readCsvLines( csvPath, error );
    if ( lines.isEmpty() )
        return true; // empty sidecar (e.g. unsupervised) is not an error
    for ( int i = 1; i < lines.size(); ++i )
    {
        const QStringList row = lines.at( i ).split( ',' );
        if ( row.size() < 2 )
            continue;
        const quint32 segId = row.at( 0 ).toUInt();
        if ( segId != 0 )
        {
            uncertainties.insert( segId, row.at( 1 ).toDouble() );
            predictedClasses.insert( segId, row.size() >= 3 ? row.at( 2 ).toInt() : 0 );
        }
    }
    return true;
}

bool parseAccuracyJson( const Json::Value &accuracy,
                        RsAccuracyAssessment::Result &result )
{
    if ( !accuracy.isObject() || !accuracy.isMember( "confusion" ) )
        return false;

    RsAccuracyAssessment::Result parsed;
    parsed.overallAccuracy = accuracy.get( "overallAccuracy", 0.0 ).asDouble();
    parsed.kappa = accuracy.get( "kappa", 0.0 ).asDouble();
    for ( const auto &id : accuracy["classes"] )
        parsed.classIds.append( id.asInt() );

    const int n = static_cast<int>( parsed.classIds.size() );
    const Json::Value &confusion = accuracy["confusion"];
    if ( n > 0 && confusion.isArray() && static_cast<int>( confusion.size() ) == n )
    {
        parsed.confusion.create( n, n, CV_32S );
        for ( int r = 0; r < n; ++r )
            for ( int c = 0; c < n; ++c )
                parsed.confusion.at<int>( r, c ) = confusion[r][c].asInt();
    }

    const auto readHash = []( const Json::Value &obj, QHash<int, double> &out ) {
        if ( !obj.isObject() )
            return;
        for ( auto it = obj.begin(); it != obj.end(); ++it )
            out.insert( std::stoi( it.key().asString() ), it->asDouble() );
    };
    readHash( accuracy["producer"], parsed.producerAcc );
    readHash( accuracy["user"], parsed.userAcc );
    readHash( accuracy["f1"], parsed.f1 );

    result = parsed;
    return !parsed.classIds.isEmpty();
}

bool rehydrateHierarchy( const QString &finePath,
                         const QString &coarsePath,
                         const QString &parentsPath,
                         RsObjectHierarchy &hierarchy,
                         QString *error )
{
    RsSegmentMap fine = RsSegmentMap::fromGeoTIFF( finePath );
    if ( fine.isEmpty() )
    {
        if ( error )
            *error = QObject::tr( "Cannot rehydrate fine level from %1" ).arg( finePath );
        return false;
    }
    QVector<RsSegmentMap> levels;
    levels.append( fine );
    if ( !coarsePath.isEmpty() )
    {
        RsSegmentMap coarse = RsSegmentMap::fromGeoTIFF( coarsePath );
        if ( coarse.isEmpty() )
        {
            if ( error )
                *error = QObject::tr( "Cannot rehydrate coarse level from %1" ).arg( coarsePath );
            return false;
        }
        levels.append( coarse );
    }

    QVector<RsParentTable> parentsTables;
    if ( levels.size() == 2 )
    {
        RsParentTable table;
        if ( !parentsPath.isEmpty() )
        {
            QFile f( parentsPath );
            if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
            {
                if ( error )
                    *error = QObject::tr( "Cannot open parents CSV %1" ).arg( parentsPath );
                return false;
            }
            QTextStream in( &f );
            int lineNo = 0;
            while ( !in.atEnd() )
            {
                const QString line = in.readLine().trimmed();
                ++lineNo;
                if ( line.isEmpty() || line.startsWith( QStringLiteral( "fine_id" ) ) )
                    continue;
                const QStringList parts = line.split( ',' );
                bool ok1 = false, ok2 = false;
                if ( parts.size() == 2 )
                {
                    const quint32 fineId = parts.at( 0 ).toUInt( &ok1 );
                    const quint32 parentId = parts.at( 1 ).toUInt( &ok2 );
                    if ( ok1 && ok2 )
                        table.fineToParent.insert( fineId, parentId );
                }
                if ( parts.size() != 2 || !ok1 || !ok2 )
                {
                    if ( error )
                        *error = QObject::tr( "Malformed parents CSV line %1" ).arg( lineNo );
                    return false;
                }
            }
        }
        parentsTables.append( table );
    }

    RsObjectHierarchy rebuilt;
    if ( !rebuilt.setLevels( std::move( levels ), std::move( parentsTables ), error ) )
        return false;
    hierarchy = std::move( rebuilt );
    return true;
}

} // namespace RsObiaOperatorAdapter
