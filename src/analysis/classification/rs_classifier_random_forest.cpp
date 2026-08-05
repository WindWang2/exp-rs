// rs_classifier_random_forest.cpp — OBIA Random Forest classifier backend.
#include "rs_classifier_random_forest.h"
#include <QDebug>
#include <algorithm>
#include <vector>

RsRandomForestBackend::RsRandomForestBackend( int numTrees, int maxDepth, int minSampleCount )
{
  m_clf = cv::ml::RTrees::create();
  m_clf->setMaxDepth( maxDepth );
  m_clf->setMinSampleCount( minSampleCount );
  m_clf->setTermCriteria( cv::TermCriteria( cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, numTrees, 0.01 ) );
}

bool RsRandomForestBackend::fit( const cv::Mat &X, const cv::Mat &y )
{
  // RTrees::getVotes returns vote counts as a function of column index, not
  // class id — so we must capture the distinct training labels here to map
  // probability columns back onto the true class space at predict time.
  cv::Mat uniqueLabels;
  try
  {
    cv::Mat yT;
    y.reshape( 1, y.total() ).convertTo( yT, CV_64F );
    std::vector<double> labels( yT.begin<double>(), yT.end<double>() );
    std::sort( labels.begin(), labels.end() );
    labels.erase( std::unique( labels.begin(), labels.end() ), labels.end() );
    uniqueLabels = cv::Mat( static_cast<int>( labels.size() ), 1, CV_32S );
    for ( size_t i = 0; i < labels.size(); ++i )
      uniqueLabels.at<int>( static_cast<int>( i ), 0 ) = static_cast<int>( labels[i] );
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsRandomForestBackend::fit — label capture failed:" << e.what();
    return false;
  }

  if ( !RsClassifierCvBackend<cv::ml::RTrees>::fit( X, y ) )
    return false;

  mClassLabels = uniqueLabels;
  return true;
}

cv::Mat RsRandomForestBackend::predictProbabilities( const cv::Mat &X ) const
{
  cv::Mat probs;
  if ( X.empty() || !m_clf || !m_clf->isTrained() )
    return probs;

  // Verify the trained label space matches getVotes' first row. RTrees reports
  // class ids in row 0 of the votes matrix; if they disagree with what we
  // captured at fit() we cannot safely map columns, so bail out rather than
  // emit a speculative guess.
  try
  {
    cv::Mat votes;
    m_clf->getVotes( X, votes, 0 );

    // votes layout: rows = (1 header) + nSamples, cols = nClasses.
    // Row 0: class ids. Rows 1..n: per-sample raw tree-vote counts.
    if ( votes.empty() || votes.rows < 2 || votes.cols < 1 )
      return probs;

    const int nClasses = votes.cols;
    if ( mClassLabels.total() != static_cast<size_t>( nClasses ) )
    {
      qWarning() << "RsRandomForestBackend::predictProbabilities — label space mismatch:"
                 << mClassLabels.total() << "trained vs" << nClasses << "votes columns";
      return probs;
    }

    const int nSamples = votes.rows - 1;
    probs = cv::Mat::zeros( nSamples, nClasses, CV_32F );
    for ( int i = 0; i < nSamples; ++i )
    {
      double totalVotes = 0.0;
      for ( int c = 0; c < nClasses; ++c )
        totalVotes += votes.at<int>( i + 1, c );
      if ( totalVotes <= 0.0 )
        continue; // leave row at uniform 0 — caller treats empty probs defensively
      for ( int c = 0; c < nClasses; ++c )
        probs.at<float>( i, c ) = static_cast<float>( votes.at<int>( i + 1, c ) / totalVotes );
    }
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsRandomForestBackend::predictProbabilities — error:" << e.what();
    probs = cv::Mat();
  }
  return probs;
}
