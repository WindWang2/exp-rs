/***************************************************************************
    qgsrpcgcptransformer.h
     --------------------------------------
    Date                 : 2026-06-03
    Copyright            : (c) 2026 SICNU GEO RS
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSRPCGCPTRANSFORMER_H
#define QGSRPCGCPTRANSFORMER_H

#include <gdal_alg.h>

#include "qgis_analysis_export.h"
#include "qgsgcptransformer.h"

#include <QString>

/**
 * \ingroup analysis
 * \brief RPC (Rational Polynomial Coefficients) physical sensor model transformer.
 *
 * Wraps GDAL's RPC transformer (`GDALCreateRPCTransformerV2`).  The RPC
 * coefficients are read from the source raster's `RPC` metadata domain
 * (RPC00B convention) rather than fitted from user GCPs; the GCP vectors
 * passed to `updateParametersFromGcps()` are ignored at v1 (Phase 11.4) and
 * exist only for API symmetry with the other transformers.  A future
 * extension may use them to refine the RPC fit.
 *
 * When a DEM path is supplied via `setDemPath()` the GDAL transformer is
 * configured with `RPC_DEM` + `RPC_DEMINTERPOLATION=bilinear` so the
 * elevation correction term is computed from the DEM raster.
 *
 * \since SICNU GEO RS Phase 11.4
 */
class QGIS_ANALYSIS_EXPORT QgsRpcGcpTransformer : public QgsGcpTransformerInterface
{
  public:
    explicit QgsRpcGcpTransformer( const QString &sourceRasterPath = QString(),
                                   const QString &demPath = QString() );
    ~QgsRpcGcpTransformer() override;

    void setSourceRasterPath( const QString &p ) { mSrc = p; }
    void setDemPath( const QString &p ) { mDem = p; }
    QString sourceRasterPath() const { return mSrc; }
    QString demPath() const { return mDem; }

    QgsGcpTransformerInterface *clone() const override;
    TransformMethod method() const override { return TransformMethod::RpcPhysical; }

    bool updateParametersFromGcps( const QVector<QgsPointXY> &source,
                                   const QVector<QgsPointXY> &destination,
                                   bool invertYAxis = false ) override;

    int minimumGcpCount() const override { return 0; }

    GDALTransformerFunc GDALTransformer() const override;
    void *GDALTransformerArgs() const override;

    //! Returns TRUE if the underlying GDAL RPC transformer has been successfully created.
    bool isValid() const { return mTransformArg != nullptr; }

  private:
    void freeTransformer();

    QString mSrc;
    QString mDem;
    void *mTransformArg = nullptr;
};

#endif // QGSRPCGCPTRANSFORMER_H
