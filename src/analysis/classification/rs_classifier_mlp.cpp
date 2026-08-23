// rs_classifier_mlp.cpp — OBIA Artificial Neural Network (ANN_MLP) classifier backend.
#include "rs_classifier_mlp.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <vector>

RsMlpBackend::RsMlpBackend( int hiddenLayerSize, int maxIter )
  : mHiddenLayerSize( std::max( 1, hiddenLayerSize ) )
{
  m_clf = cv::ml::ANN_MLP::create();
  m_clf->setActivationFunction( cv::ml::ANN_MLP::SIGMOID_SYM, 1.0, 1.0 );
  m_clf->setTermCriteria( cv::TermCriteria( cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, maxIter, 0.01 ) );
  m_clf->setTrainMethod( cv::ml::ANN_MLP::RPROP, 0.1 );
}

bool RsMlpBackend::fit( const cv::Mat &X, const cv::Mat &y )
{
  if ( X.empty() || y.empty() || X.rows != y.rows || !m_clf )
    return false;

  // Capture the distinct training labels in ascending order so the network's
  // output columns map back onto true class ids deterministically (and
  // independent of whether labels happen to be 0-based or 1-based).
  std::vector<int> labels;
  labels.reserve( y.rows );
  for ( int i = 0; i < y.rows; ++i )
    labels.push_back( y.at<int>( i, 0 ) );
  std::sort( labels.begin(), labels.end() );
  labels.erase( std::unique( labels.begin(), labels.end() ), labels.end() );
  const int numClasses = std::max( 2, static_cast<int>( labels.size() ) );

  mClassLabels = cv::Mat( numClasses, 1, CV_32S );
  for ( int c = 0; c < static_cast<int>( labels.size() ); ++c )
    mClassLabels.at<int>( c, 0 ) = labels[c];
  // Pad with sentinel ids if the training set happened to have <2 distinct
  // labels (ANN_MLP requires ≥2 outputs); those columns are never the argmax.
  for ( int c = static_cast<int>( labels.size() ); c < numClasses; ++c )
    mClassLabels.at<int>( c, 0 ) = labels.empty() ? 0 : labels.front();

  cv::Mat layerSizes = ( cv::Mat_<int>( 1, 3 ) << X.cols, mHiddenLayerSize, numClasses );
  m_clf->setLayerSizes( layerSizes );

  // One-hot encode: column c is hot iff y[i] == mClassLabels[c].
  cv::Mat responses = cv::Mat::zeros( y.rows, numClasses, CV_32F );
  for ( int i = 0; i < y.rows; ++i )
  {
    const int cls = y.at<int>( i, 0 );
    auto it = std::lower_bound( labels.begin(), labels.end(), cls );
    if ( it != labels.end() && *it == cls )
      responses.at<float>( i, static_cast<int>( it - labels.begin() ) ) = 1.0f;
  }

  try
  {
    if ( !m_clf->train( X, cv::ml::ROW_SAMPLE, responses ) )
      return false;
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsMlpBackend::fit — OpenCV error:" << e.what();
    return false;
  }

  // Sanity probe: OpenCV's ANN_MLP is known to produce all-NaN output models
  // on some builds (notably OpenCV 5.0.0 with SIGMOID_SYM + RPROP/BACKPROP),
  // making every downstream prediction meaningless. Detect that here and fail
  // fit() loudly rather than silently shipping a broken model.
  cv::Mat probe;
  m_clf->predict( X.row( 0 ), probe );
  if ( probe.empty() )
    return false;
  bool anyFinite = false;
  for ( int c = 0; c < probe.cols; ++c )
  {
    if ( std::isfinite( probe.at<float>( 0, c ) ) )
    {
      anyFinite = true;
      break;
    }
  }
  if ( !anyFinite )
  {
    qWarning() << "RsMlpBackend::fit — trained model produces non-finite output "
                  "(known OpenCV ANN_MLP issue); refusing to report a fitted model";
    mClassLabels.release();
    m_clf = nullptr; // drop the NaN model so isFitted()/predict() report unfit
    return false;
  }
  return true;
}

cv::Mat RsMlpBackend::predict( const cv::Mat &X ) const
{
  // ANN_MLP::predict writes a [N × numClasses] float activation matrix; the
  // base CvBackend::predict would convertTo(CV_32S) that, yielding nonsense
  // class ids. Argmax each row and map back through mClassLabels instead.
  cv::Mat out;
  if ( X.empty() || !m_clf || !m_clf->isTrained() || mClassLabels.empty() )
    return out;
  try
  {
    cv::Mat raw;
    m_clf->predict( X, raw );
    if ( raw.empty() || raw.rows != X.rows )
      return out;
    out = cv::Mat( raw.rows, 1, CV_32S );
    for ( int i = 0; i < raw.rows; ++i )
    {
      int bestCol = 0;
      float bestVal = raw.at<float>( i, 0 );
      for ( int c = 1; c < raw.cols; ++c )
      {
        if ( raw.at<float>( i, c ) > bestVal )
        {
          bestVal = raw.at<float>( i, c );
          bestCol = c;
        }
      }
      out.at<int>( i, 0 ) = mClassLabels.at<int>( bestCol, 0 );
    }
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsMlpBackend::predict — error:" << e.what();
    out = cv::Mat();
  }
  return out;
}

cv::Mat RsMlpBackend::predictProbabilities( const cv::Mat &X ) const
{
  cv::Mat probs;
  if ( X.empty() || !m_clf || !m_clf->isTrained() )
    return probs;

  try
  {
    cv::Mat rawOutput;
    m_clf->predict( X, rawOutput );
    if ( rawOutput.empty() )
      return probs;

    const int numOut = rawOutput.cols;
    probs = cv::Mat::zeros( X.rows, numOut, CV_32F );
    for ( int i = 0; i < X.rows; ++i )
    {
      float maxVal = -1e9f;
      for ( int c = 0; c < numOut; ++c )
      {
        float val = rawOutput.at<float>( i, c );
        if ( std::isfinite( val ) )
          maxVal = std::max( maxVal, val );
      }
      if ( maxVal < -1e8f )
        maxVal = 0.0f;

      float sumExp = 0.0f;
      for ( int c = 0; c < numOut; ++c )
      {
        float val = rawOutput.at<float>( i, c );
        float eVal = 0.0f;
        if ( std::isfinite( val ) )
        {
          float diff = val - maxVal;
          eVal = std::exp( std::clamp( diff, -20.0f, 0.0f ) );
        }
        probs.at<float>( i, c ) = eVal;
        sumExp += eVal;
      }
      if ( sumExp > 1e-6f )
      {
        for ( int c = 0; c < numOut; ++c )
          probs.at<float>( i, c ) /= sumExp;
      }
      else
      {
        for ( int c = 0; c < numOut; ++c )
          probs.at<float>( i, c ) = 1.0f / numOut;
      }
    }
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsMlpBackend::predictProbabilities — error:" << e.what();
    probs = cv::Mat();
  }
  return probs;
}

bool RsMlpBackend::predictWithProbabilities( const cv::Mat &X, cv::Mat &outLabels,
                                             cv::Mat &outProbs ) const
{
  outLabels.release();
  outProbs.release();
  if ( X.empty() || !m_clf || !m_clf->isTrained() || mClassLabels.empty() )
    return false;
  try
  {
    cv::Mat raw;
    m_clf->predict( X, raw );
    if ( raw.empty() || raw.rows != X.rows )
      return false;
    const int n = raw.rows;
    const int k = raw.cols;
    outLabels.create( n, 1, CV_32S );
    outProbs.create( n, k, CV_32F );
    for ( int i = 0; i < n; ++i )
    {
      // argmax + softmax from same raw row
      float maxVal = raw.at<float>( i, 0 );
      for ( int c = 1; c < k; ++c )
        maxVal = std::max( maxVal, raw.at<float>( i, c ) );
      if ( !std::isfinite( maxVal ) )
        maxVal = 0.0f;
      float sumExp = 0.0f;
      for ( int c = 0; c < k; ++c )
      {
        float v = raw.at<float>( i, c );
        float diff = std::isfinite( v ) ? ( v - maxVal ) : -20.0f;
        diff = std::clamp( diff, -20.0f, 0.0f );
        float e = std::exp( diff );
        outProbs.at<float>( i, c ) = e;
        sumExp += e;
      }
      if ( sumExp > 1e-6f )
        for ( int c = 0; c < k; ++c )
          outProbs.at<float>( i, c ) /= sumExp;
      else
        for ( int c = 0; c < k; ++c )
          outProbs.at<float>( i, c ) = 1.0f / k;
      int bestCol = 0;
      float bestVal = raw.at<float>( i, 0 );
      for ( int c = 1; c < k; ++c )
        if ( raw.at<float>( i, c ) > bestVal )
        {
          bestVal = raw.at<float>( i, c );
          bestCol = c;
        }
      outLabels.at<int>( i, 0 ) = mClassLabels.at<int>( bestCol, 0 );
    }
    return true;
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsMlpBackend::predictWithProbabilities — error:" << e.what();
    outLabels.release();
    outProbs.release();
    return false;
  }
}

bool RsMlpBackend::save( const QString &path ) const
{
  if ( !RsClassifierCvBackend<cv::ml::ANN_MLP>::save( path ) )
    return false;
  if ( mClassLabels.empty() )
    return true;
  QFile f( path + QStringLiteral( ".labels.json" ) );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return true;
  QJsonArray arr;
  for ( int i = 0; i < mClassLabels.rows; ++i )
    arr.append( mClassLabels.at<int>( i, 0 ) );
  f.write( QJsonDocument( arr ).toJson( QJsonDocument::Compact ) );
  return true;
}

bool RsMlpBackend::load( const QString &path )
{
  if ( !RsClassifierCvBackend<cv::ml::ANN_MLP>::load( path ) )
    return false;
  QFile f( path + QStringLiteral( ".labels.json" ) );
  if ( !f.open( QIODevice::ReadOnly ) )
    return true;
  const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
  if ( !doc.isArray() )
    return true;
  const QJsonArray arr = doc.array();
  if ( arr.isEmpty() )
    return true;
  mClassLabels.create( static_cast<int>( arr.size() ), 1, CV_32S );
  for ( int i = 0; i < arr.size(); ++i )
    mClassLabels.at<int>( i, 0 ) = arr[i].toInt();
  return true;
}
