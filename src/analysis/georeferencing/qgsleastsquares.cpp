/***************************************************************************
     qgsleastsquares.cpp
     --------------------------------------
    Date                 : Sun Sep 16 12:03:37 AKDT 2007
    Copyright            : (C) 2007 by Gary E. Sherman
    Email                : sherman at mrcc dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsconfig.h"
#include "qgsleastsquares.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "qgsexception.h"

#include <QObject>

#ifdef HAVE_GSL
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_blas.h>
#endif

void QgsLeastSquares::linear( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, QgsPointXY &origin, double &pixelXSize, double &pixelYSize )
{
  const int n = destinationCoordinates.size();
  if ( n < 2 )
  {
    throw std::domain_error( QObject::tr( "Fit to a linear transform requires at least 2 points." ).toLocal8Bit().constData() );
  }

  double meanSrcX = 0.0, meanSrcY = 0.0;
  double meanDstX = 0.0, meanDstY = 0.0;
  for ( int i = 0; i < n; ++i )
  {
    meanSrcX += sourceCoordinates.at( i ).x();
    meanSrcY += sourceCoordinates.at( i ).y();
    meanDstX += destinationCoordinates.at( i ).x();
    meanDstY += destinationCoordinates.at( i ).y();
  }
  meanSrcX /= n;
  meanSrcY /= n;
  meanDstX /= n;
  meanDstY /= n;

  double sumNormX = 0.0, sumCrossX = 0.0;
  double sumNormY = 0.0, sumCrossY = 0.0;
  for ( int i = 0; i < n; ++i )
  {
    const double dx = sourceCoordinates.at( i ).x() - meanSrcX;
    const double dy = sourceCoordinates.at( i ).y() - meanSrcY;
    const double dX = destinationCoordinates.at( i ).x() - meanDstX;
    const double dY = destinationCoordinates.at( i ).y() - meanDstY;

    sumNormX += dx * dx;
    sumCrossX += dx * dX;
    sumNormY += dy * dy;
    sumCrossY += dy * dY;
  }

  // SICNU_GEO_RS: detect singular systems (collinear / duplicate source points)
  // and report them via QgsLeastSquares::SingularException so callers can
  // surface a meaningful error instead of producing NaN/Inf parameters.
  if ( sumNormX < 1e-12 || sumNormY < 1e-12 )
  {
    throw QgsLeastSquares::SingularException();
  }

  const double bX = sumCrossX / sumNormX;
  const double bY = sumCrossY / sumNormY;
  const double aX = meanDstX - bX * meanSrcX;
  const double aY = meanDstY - bY * meanSrcY;

  if ( !std::isfinite( aX ) || !std::isfinite( aY ) || !std::isfinite( bX ) || !std::isfinite( bY ) )
  {
    throw QgsLeastSquares::SingularException();
  }

  origin.setX( aX );
  origin.setY( aY );

  // Preserve signed scales so mirrored / flipped source axes remain correct.
  // Callers that need a positive pixel size should take fabs themselves.
  pixelXSize = bX;
  pixelYSize = bY;
}


void QgsLeastSquares::helmert( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, QgsPointXY &origin, double &pixelSize, double &rotation )
{
  const int n = destinationCoordinates.size();
  if ( n < 2 )
  {
    throw std::domain_error( QObject::tr( "Fit to a Helmert transform requires at least 2 points." ).toLocal8Bit().constData() );
  }

  double meanSrcX = 0.0, meanSrcY = 0.0;
  double meanDstX = 0.0, meanDstY = 0.0;
  for ( int i = 0; i < n; ++i )
  {
    meanSrcX += sourceCoordinates.at( i ).x();
    meanSrcY += sourceCoordinates.at( i ).y();
    meanDstX += destinationCoordinates.at( i ).x();
    meanDstY += destinationCoordinates.at( i ).y();
  }
  meanSrcX /= n;
  meanSrcY /= n;
  meanDstX /= n;
  meanDstY /= n;

  double sumCross = 0.0;
  double sumRot = 0.0;
  double sumNormSq = 0.0;

  for ( int i = 0; i < n; ++i )
  {
    const double dx = sourceCoordinates.at( i ).x() - meanSrcX;
    const double dy = sourceCoordinates.at( i ).y() - meanSrcY;
    const double dX = destinationCoordinates.at( i ).x() - meanDstX;
    const double dY = destinationCoordinates.at( i ).y() - meanDstY;

    sumCross += dx * dX + dy * dY;
    sumRot += dx * dY - dy * dX;
    sumNormSq += dx * dx + dy * dy;
  }

  if ( sumNormSq < 1e-12 )
  {
    throw QgsLeastSquares::SingularException();
  }

  const double a = sumCross / sumNormSq;
  const double bParam = sumRot / sumNormSq;
  const double x0 = meanDstX - ( a * meanSrcX - bParam * meanSrcY );
  const double y0 = meanDstY - ( bParam * meanSrcX + a * meanSrcY );

  if ( !std::isfinite( a ) || !std::isfinite( bParam ) || !std::isfinite( x0 ) || !std::isfinite( y0 ) )
  {
    throw QgsLeastSquares::SingularException();
  }

  origin.setX( x0 );
  origin.setY( y0 );
  pixelSize = std::hypot( a, bParam );
  rotation = std::atan2( bParam, a );
}

#if 0
void QgsLeastSquares::affine( QVector<QgsPointXY> mapCoords,
                              QVector<QgsPointXY> pixelCoords )
{
  int n = mapCoords.size();
  if ( n < 4 )
  {
    throw std::domain_error( QObject::tr( "Fit to an affine transform requires at least 4 points." ).toLocal8Bit().constData() );
  }

  double A = 0, B = 0, C = 0, D = 0, E = 0, F = 0,
         G = 0, H = 0, I = 0, J = 0, K = 0;
  for ( int i = 0; i < n; ++i )
  {
    A += pixelCoords[i].x();
    B += pixelCoords[i].y();
    C += mapCoords[i].x();
    D += mapCoords[i].y();
    E += std::pow( pixelCoords[i].x(), 2 );
    F += std::pow( pixelCoords[i].y(), 2 );
    G += pixelCoords[i].x() * pixelCoords[i].y();
    H += pixelCoords[i].x() * mapCoords[i].x();
    I += pixelCoords[i].y() * mapCoords[i].y();
    J += pixelCoords[i].x() * mapCoords[i].y();
    K += mapCoords[i].x() * pixelCoords[i].y();
  }

  /* The least squares fit for the parameters { a, b, c, d, x0, y0 } is the
     solution to the matrix equation Mx = b, where M and b is given below.
     I *think* that this is correct but I derived it myself late at night.
     Look at affine.jpg if you suspect bugs. */

  double MData[] = { A,    B,    0,    0, ( double ) n,    0,
                     0,    0,    A,    B,    0, ( double ) n,
                     E,    G,    0,    0,    A,    0,
                     G,    F,    0,    0,    B,    0,
                     0,    0,    E,    G,    0,    A,
                     0,    0,    G,    F,    0,    B
                   };

  double bData[] = { C,    D,    H,    K,    J,    I };

  // we want to solve the equation M*x = b, where x = [a b c d x0 y0]
  gsl_matrix_view M = gsl_matrix_view_array( MData, 6, 6 );
  gsl_vector_view b = gsl_vector_view_array( bData, 6 );
  gsl_vector *x = gsl_vector_alloc( 6 );
  gsl_permutation *p = gsl_permutation_alloc( 6 );
  int s;
  gsl_linalg_LU_decomp( &M.matrix, p, &s );
  gsl_linalg_LU_solve( &M.matrix, p, &b.vector, x );
  gsl_permutation_free( p );

}
#endif

/**
 * Scales the given coordinates so that the center of gravity is at the origin and the mean distance to the origin is sqrt(2).
 *
 * Also returns 3x3 homogeneous matrices which can be used to normalize and de-normalize coordinates.
 */
void normalizeCoordinates( const QVector<QgsPointXY> &coords, QVector<QgsPointXY> &normalizedCoords, double normalizeMatrix[9], double denormalizeMatrix[9] )
{
  // Calculate center of gravity
  double cogX = 0.0, cogY = 0.0;
  for ( int i = 0; i < coords.size(); i++ )
  {
    cogX += coords[i].x();
    cogY += coords[i].y();
  }
  cogX *= 1.0 / coords.size();
  cogY *= 1.0 / coords.size();

  // Calculate mean distance to origin
  double meanDist = 0.0;
  for ( int i = 0; i < coords.size(); i++ )
  {
    const double X = ( coords[i].x() - cogX );
    const double Y = ( coords[i].y() - cogY );
    meanDist += std::sqrt( X * X + Y * Y );
  }
  meanDist *= 1.0 / coords.size();

  // All points coincide (or are numerically identical) → projective scale
  // would divide by zero / produce Inf/NaN coefficients.
  if ( meanDist < std::numeric_limits<double>::epsilon() )
  {
    throw QgsLeastSquares::SingularException();
  }

  const double OOD = meanDist * M_SQRT1_2;
  const double D = 1.0 / OOD;
  normalizedCoords.resize( coords.size() );
  for ( int i = 0; i < coords.size(); i++ )
  {
    normalizedCoords[i] = QgsPointXY( ( coords[i].x() - cogX ) * D, ( coords[i].y() - cogY ) * D );
  }

  normalizeMatrix[0] = D;
  normalizeMatrix[1] = 0.0;
  normalizeMatrix[2] = -cogX * D;
  normalizeMatrix[3] = 0.0;
  normalizeMatrix[4] = D;
  normalizeMatrix[5] = -cogY * D;
  normalizeMatrix[6] = 0.0;
  normalizeMatrix[7] = 0.0;
  normalizeMatrix[8] = 1.0;

  denormalizeMatrix[0] = OOD;
  denormalizeMatrix[1] = 0.0;
  denormalizeMatrix[2] = cogX;
  denormalizeMatrix[3] = 0.0;
  denormalizeMatrix[4] = OOD;
  denormalizeMatrix[5] = cogY;
  denormalizeMatrix[6] = 0.0;
  denormalizeMatrix[7] = 0.0;
  denormalizeMatrix[8] = 1.0;
}

// Fits a homography to the given corresponding points, and
// return it in H (row-major format).
void QgsLeastSquares::projective( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, double H[9] )
{
#ifndef HAVE_GSL
  ( void ) sourceCoordinates;
  ( void ) destinationCoordinates;
  ( void ) H;
  throw QgsNotSupportedException( QObject::tr( "Calculating a projective transformation requires a QGIS build based GSL" ) );
#else
  Q_ASSERT( sourceCoordinates.size() == destinationCoordinates.size() );

  if ( destinationCoordinates.size() < 4 )
  {
    throw std::domain_error( QObject::tr( "Fitting a projective transform requires at least 4 corresponding points." ).toLocal8Bit().constData() );
  }

  QVector<QgsPointXY> sourceCoordinatesNormalized;
  QVector<QgsPointXY> destinationCoordinatesNormalized;

  double normSource[9], denormSource[9];
  double normDest[9], denormDest[9];
  normalizeCoordinates( sourceCoordinates, sourceCoordinatesNormalized, normSource, denormSource );
  normalizeCoordinates( destinationCoordinates, destinationCoordinatesNormalized, normDest, denormDest );

  // GSL does not support a full SVD, so we artificially add a linear dependent row
  // to the matrix in case the system is underconstrained.
  const uint m = std::max( 9u, ( uint ) destinationCoordinatesNormalized.size() * 2u );
  const uint n = 9;
  gsl_matrix *S = gsl_matrix_alloc( m, n );

  for ( int i = 0; i < destinationCoordinatesNormalized.size(); i++ )
  {
    gsl_matrix_set( S, i * 2, 0, sourceCoordinatesNormalized[i].x() );
    gsl_matrix_set( S, i * 2, 1, sourceCoordinatesNormalized[i].y() );
    gsl_matrix_set( S, i * 2, 2, 1.0 );

    gsl_matrix_set( S, i * 2, 3, 0.0 );
    gsl_matrix_set( S, i * 2, 4, 0.0 );
    gsl_matrix_set( S, i * 2, 5, 0.0 );

    gsl_matrix_set( S, i * 2, 6, -destinationCoordinatesNormalized[i].x() * sourceCoordinatesNormalized[i].x() );
    gsl_matrix_set( S, i * 2, 7, -destinationCoordinatesNormalized[i].x() * sourceCoordinatesNormalized[i].y() );
    gsl_matrix_set( S, i * 2, 8, -destinationCoordinatesNormalized[i].x() * 1.0 );

    gsl_matrix_set( S, i * 2 + 1, 0, 0.0 );
    gsl_matrix_set( S, i * 2 + 1, 1, 0.0 );
    gsl_matrix_set( S, i * 2 + 1, 2, 0.0 );

    gsl_matrix_set( S, i * 2 + 1, 3, sourceCoordinatesNormalized[i].x() );
    gsl_matrix_set( S, i * 2 + 1, 4, sourceCoordinatesNormalized[i].y() );
    gsl_matrix_set( S, i * 2 + 1, 5, 1.0 );

    gsl_matrix_set( S, i * 2 + 1, 6, -destinationCoordinatesNormalized[i].y() * sourceCoordinatesNormalized[i].x() );
    gsl_matrix_set( S, i * 2 + 1, 7, -destinationCoordinatesNormalized[i].y() * sourceCoordinatesNormalized[i].y() );
    gsl_matrix_set( S, i * 2 + 1, 8, -destinationCoordinatesNormalized[i].y() * 1.0 );
  }

  if ( destinationCoordinatesNormalized.size() == 4 )
  {
    // The GSL SVD routine only supports matrices with rows >= columns (m >= n)
    // Unfortunately, we can't use the SVD of the transpose (i.e. S^T = (U D V^T)^T = V D U^T)
    // to work around this, because the solution lies in the right nullspace of S, and
    // gsl only supports a thin SVD of S^T, which does not return these vectors.

    // HACK: duplicate last row to get a 9x9 equation system
    for ( int j = 0; j < 9; j++ )
    {
      gsl_matrix_set( S, 8, j, gsl_matrix_get( S, 7, j ) );
    }
  }

  // Solve Sh = 0 in the total least squares sense, i.e.
  // with Sh = min and |h|=1. The solution "h" is given by the
  // right singular eigenvector of S corresponding, to the smallest
  // singular value (via SVD).
  gsl_matrix *V = gsl_matrix_alloc( n, n );
  gsl_vector *singular_values = gsl_vector_alloc( n );
  gsl_vector *work = gsl_vector_alloc( n );

  // V = n x n
  // U = m x n (thin SVD)  U D V^T
  gsl_linalg_SV_decomp( S, V, singular_values, work );

  // Check for rank deficiency: in normalized coordinates, a non-degenerate 4+ point
  // system has rank 8 (nullity 1). If the second smallest singular value (index 7)
  // is zero / near-zero relative to the largest, the points are collinear or degenerate.
  const double s0 = gsl_vector_get( singular_values, 0 );
  const double s_penultimate = gsl_vector_get( singular_values, n - 2 );

  if ( !std::isfinite( s0 ) || s0 <= 1e-12 || !std::isfinite( s_penultimate ) || s_penultimate < 1e-6 * s0 )
  {
    gsl_matrix_free( S );
    gsl_matrix_free( V );
    gsl_vector_free( singular_values );
    gsl_vector_free( work );
    throw QgsLeastSquares::SingularException();
  }

  // Columns of V store the right singular vectors of S
  for ( unsigned int i = 0; i < n; i++ )
  {
    H[i] = gsl_matrix_get( V, i, n - 1 );
  }

  gsl_matrix *prodMatrix = gsl_matrix_alloc( 3, 3 );

  gsl_matrix_view Hmatrix = gsl_matrix_view_array( H, 3, 3 );
  const gsl_matrix_view normSourceMatrix = gsl_matrix_view_array( normSource, 3, 3 );
  const gsl_matrix_view denormDestMatrix = gsl_matrix_view_array( denormDest, 3, 3 );

  // Change coordinate frame of image and pre-image from normalized to destination and source coordinates.
  // H' = denormalizeMapCoords*H*normalizePixelCoords
  gsl_blas_dgemm( CblasNoTrans, CblasNoTrans, 1.0, &Hmatrix.matrix, &normSourceMatrix.matrix, 0.0, prodMatrix );
  gsl_blas_dgemm( CblasNoTrans, CblasNoTrans, 1.0, &denormDestMatrix.matrix, prodMatrix, 0.0, &Hmatrix.matrix );

  gsl_matrix_free( prodMatrix );
  gsl_matrix_free( S );
  gsl_matrix_free( V );
  gsl_vector_free( singular_values );
  gsl_vector_free( work );
#endif
}
