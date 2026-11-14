// test_r30_t01_postprocess_windows.cpp — R30-T01 WP08/WP09: cancel-aware
// clump with the int32 component-count guard, and the global-labeling sieve
// rewrite proven BIT-EXACT against an independent in-test reimplementation
// of the legacy per-class algorithm (frozen-orig components, live work-copy
// neighborhood, per-class processing order), plus hand-computed oracles for
// tie votes and sequential replacement order.
#include "rs_post_process.h"

#include <catch2/catch_test_macros.hpp>

#include <QMap>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace
{

cv::Mat matFromRows( const std::vector<std::vector<int>> &rows )
{
  const int H = int( rows.size() );
  const int W = H > 0 ? int( rows[0].size() ) : 0;
  cv::Mat m( H, W, CV_32S );
  for ( int r = 0; r < H; ++r )
    for ( int c = 0; c < W; ++c )
      m.at<int>( r, c ) = rows[static_cast<size_t>( r )][static_cast<size_t>( c )];
  return m;
}

/// Independent reimplementation of the LEGACY sieve (pre-R30 master):
/// per-class binary CC on a frozen copy, sequential work-copy neighborhood,
/// classes in raster first-occurrence order. Kept in the TEST as the
/// equivalence oracle for the production global-labeling rewrite.
bool legacySieveOracle( const cv::Mat &src, cv::Mat &dst, int threshold, int connectedness )
{
  cv::Mat labels;
  if ( src.type() == CV_32S )
    labels = src;
  else
    src.convertTo( labels, CV_32S );
  if ( threshold < 1 )
  {
    dst = labels.clone();
    return true;
  }
  cv::Mat work = labels.clone();
  std::vector<int> classIds;
  {
    std::unordered_map<int, char> seen;
    for ( int r = 0; r < work.rows; ++r )
    {
      const int *row = work.ptr<int>( r );
      for ( int c = 0; c < work.cols; ++c )
        if ( seen.emplace( row[c], 1 ).second )
          classIds.push_back( row[c] );
    }
  }
  const cv::Mat orig = work.clone();

  auto majority = [&]( int compId, const cv::Mat &cc, const cv::Mat &stats,
                       int fallback ) {
    static const int dr8[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    static const int dc8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int dr4[4] = { -1, 1, 0, 0 };
    static const int dc4[4] = { 0, 0, -1, 1 };
    const int nDir = connectedness == 8 ? 8 : 4;
    const int *dr = connectedness == 8 ? dr8 : dr4;
    const int *dc = connectedness == 8 ? dc8 : dc4;
    const int top = stats.at<int>( compId, cv::CC_STAT_TOP );
    const int left = stats.at<int>( compId, cv::CC_STAT_LEFT );
    const int height = stats.at<int>( compId, cv::CC_STAT_HEIGHT );
    const int width = stats.at<int>( compId, cv::CC_STAT_WIDTH );
    std::unordered_map<int, int> freq;
    for ( int r = top; r < top + height; ++r )
      for ( int c = left; c < left + width; ++c )
      {
        if ( cc.at<int>( r, c ) != compId )
          continue;
        for ( int d = 0; d < nDir; ++d )
        {
          const int nr = r + dr[d], nc = c + dc[d];
          if ( nr < 0 || nr >= work.rows || nc < 0 || nc >= work.cols )
            continue;
          if ( cc.at<int>( nr, nc ) == compId )
            continue;
          const int vote = work.at<int>( nr, nc );
          if ( vote == 0 )
            continue;
          ++freq[vote];
        }
      }
    if ( freq.empty() )
      return fallback;
    int bestVal = 0, bestCnt = -1;
    for ( const auto &kv : freq )
      if ( kv.second > bestCnt || ( kv.second == bestCnt && kv.first < bestVal ) )
      {
        bestCnt = kv.second;
        bestVal = kv.first;
      }
    return bestVal;
  };

  for ( int classId : classIds )
  {
    cv::Mat mask;
    cv::compare( orig, classId, mask, cv::CMP_EQ );
    cv::Mat cc, stats, centroids;
    const int nComp = cv::connectedComponentsWithStats( mask, cc, stats, centroids,
                                                        connectedness, CV_32S );
    for ( int comp = 1; comp < nComp; ++comp )
    {
      const int area = stats.at<int>( comp, cv::CC_STAT_AREA );
      if ( area >= threshold )
        continue;
      const int replacement = majority( comp, cc, stats, classId );
      const int top = stats.at<int>( comp, cv::CC_STAT_TOP );
      const int left = stats.at<int>( comp, cv::CC_STAT_LEFT );
      const int height = stats.at<int>( comp, cv::CC_STAT_HEIGHT );
      const int width = stats.at<int>( comp, cv::CC_STAT_WIDTH );
      for ( int r = top; r < top + height; ++r )
      {
        const int *ccRow = cc.ptr<int>( r );
        int *outRow = work.ptr<int>( r );
        for ( int c = left; c < left + width; ++c )
          if ( ccRow[c] == comp )
            outRow[c] = replacement;
      }
    }
  }
  dst = work;
  return true;
}

bool matsEqual( const cv::Mat &a, const cv::Mat &b )
{
  if ( a.rows != b.rows || a.cols != b.cols || a.type() != b.type() )
    return false;
  return cv::norm( a, b, cv::NORM_INF ) == 0.0;
}

} // namespace

// --- WP09: sieve equivalence ----------------------------------------------------

TEST_CASE( "global-labeling sieve is bit-exact against the legacy per-class algorithm",
           "[postprocess][sieve][r30t01]" )
{
  // Deterministic random maps across class counts, thresholds and both
  // connectivities — including zero-label (NoData) pixels and class 0
  // neighborhoods so the non-voting rule is exercised.
  std::mt19937 rng( 20261113u );
  for ( int iteration = 0; iteration < 60; ++iteration )
  {
    const int H = 8 + int( rng() % 49 );
    const int W = 8 + int( rng() % 49 );
    const int classes = 1 + int( rng() % 6 );
    const int threshold = 1 + int( rng() % 12 );
    const int connectedness = ( rng() % 2 ) == 0 ? 4 : 8;

    cv::Mat src( H, W, CV_32S );
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        src.at<int>( r, c ) = int( rng() % ( classes + 1 ) ); // 0..classes

    cv::Mat legacy, produced;
    REQUIRE( legacySieveOracle( src, legacy, threshold, connectedness ) );
    QString err;
    REQUIRE( RsPostProcess::sieve( src, produced, threshold, connectedness, &err ) );
    if ( !matsEqual( legacy, produced ) )
    {
      CAPTURE( iteration, H, W, classes, threshold, connectedness );
      FAIL( "sieve rewrite diverged from the legacy oracle" );
    }
  }
}

TEST_CASE( "sieve tie votes break toward the smaller label and sequential order is pinned",
           "[postprocess][sieve][r30t01]" )
{
  // Hand-computed oracle 1 — TIE inside a CASCADE: all three single-pixel
  // classes (2, 5, 3) are below threshold 2 (4-conn). Classes process in
  // raster first-occurrence order (2, then 5, then 3), each reading the
  // LIVE work copy:
  //   2 @ (1,1): only vote is the 5 → becomes 5
  //   5 @ (1,2): votes {5 (the replaced 2): 1, 3: 1} → TIE → smaller = 3
  //   3 @ (1,3): only vote is itself (the replaced 5) → stays 3
  //   0 0 0 0 0
  //   0 2 5 3 0
  //   0 0 0 0 0
  const cv::Mat tie = matFromRows( { { 0, 0, 0, 0, 0 },
                                     { 0, 2, 5, 3, 0 },
                                     { 0, 0, 0, 0, 0 } } );
  cv::Mat out;
  QString err;
  REQUIRE( RsPostProcess::sieve( tie, out, 2, 4, &err ) );
  CHECK( out.at<int>( 1, 1 ) == 5 ); // 2 → its only voter, the 5
  CHECK( out.at<int>( 1, 2 ) == 3 ); // tie {5,3} → smaller label 3
  CHECK( out.at<int>( 1, 3 ) == 3 ); // 3 stays (self-vote via replaced 5)

  // Hand-computed oracle 2 — SEQUENTIAL REPLACEMENT with a tie: patches
  // 1 (area 2) and 2 (area 2) sit between the big 9 background. Order: 9
  // first (area 8 survives), then 1, then 2. The 1-column's border votes:
  // left = 9 (one per patch pixel = 2), right = 2 (2) → TIE {9:2, 2:2} →
  // smaller label 2 wins → the 1s become 2. The 2-column then sees its
  // left neighbors already 2 → stays 2. NOTE the right-edge 9-PAIR is its
  // OWN component (area 2 < 3, cut off by the 2-column): its only border
  // votes are the (now) 2s → it becomes 2 as well.
  //   9 9 1 2 9        9 9 2 2 2
  //   9 9 1 2 9   →    9 9 2 2 2
  const cv::Mat seq = matFromRows( { { 9, 9, 1, 2, 9 },
                                     { 9, 9, 1, 2, 9 } } );
  REQUIRE( RsPostProcess::sieve( seq, out, 3, 4, &err ) );
  for ( int r = 0; r < out.rows; ++r )
  {
    CHECK( out.at<int>( r, 0 ) == 9 );
    CHECK( out.at<int>( r, 1 ) == 9 );
    CHECK( out.at<int>( r, 2 ) == 2 ); // former 1s: tie → smaller label 2
    CHECK( out.at<int>( r, 3 ) == 2 );
    CHECK( out.at<int>( r, 4 ) == 2 ); // isolated 9-pair (area 2) → its voter 2
  }

  // Hand-computed oracle 3 — MAJORITY FROM THE LIVE COPY: patch A (class 1)
  // is replaced first (border majority 7), then adjacent patch B (class 2,
  // same size) computes its majority over the ALREADY-REPLACED 7 pixels —
  // sequential semantics, not the frozen input.
  //   7 7 7 7 7
  //   7 1 1 2 7
  //   7 7 7 7 7
  // threshold 3: A area 2 → borders 7×6 (and 2) → 7. B area 2 → borders
  // 7×6 (A is now 7) → 7.
  const cv::Mat live = matFromRows( { { 7, 7, 7, 7, 7 },
                                      { 7, 1, 1, 2, 7 },
                                      { 7, 7, 7, 7, 7 } } );
  REQUIRE( RsPostProcess::sieve( live, out, 3, 4, &err ) );
  for ( int r = 0; r < out.rows; ++r )
    for ( int c = 0; c < out.cols; ++c )
      CHECK( out.at<int>( r, c ) == 7 );

  // Hand-computed oracle 4 — NoData (0) never votes and never wins: a class-4
  // island inside label-0 territory keeps itself only via its own fallback
  // when NO valid vote exists; surrounded purely by 0 → fallback = own class.
  //   0 0 0
  //   0 4 0
  //   0 0 0
  const cv::Mat island = matFromRows( { { 0, 0, 0 }, { 0, 4, 0 }, { 0, 0, 0 } } );
  REQUIRE( RsPostProcess::sieve( island, out, 2, 4, &err ) );
  CHECK( out.at<int>( 1, 1 ) == 4 ); // no votes → fallback to own class
}

TEST_CASE( "sieve cancellation reports Cancelled and keeps dst untouched",
           "[postprocess][sieve][r30t01]" )
{
  cv::Mat src = matFromRows( { { 1, 2, 1 }, { 2, 1, 2 }, { 1, 2, 1 } } );
  cv::Mat out = matFromRows( { { 5, 5, 5 }, { 5, 5, 5 }, { 5, 5, 5 } } );
  QString err;
  CHECK_FALSE( RsPostProcess::sieve( src, out, 2, 4, &err,
                                     []() { return true; } ) );
  CHECK( err == QStringLiteral( "Cancelled" ) );
  // dst was pre-set: a cancelled run must not hand back partial output.
  CHECK( out.at<int>( 0, 0 ) == 5 );
}

// --- WP08: clump -----------------------------------------------------------------

TEST_CASE( "clump component ids are deterministic raster-seed order",
           "[postprocess][clump][r30t01]" )
{
  // Hand oracle: multi-label CC — label 0 forms components TOO. 4-conn
  // seed order: (0,0)8→1, (0,1)0→2, (0,2)7→3, (0,3)0→4, (1,0)0→5,
  // (1,1)8→6, {(1,2),(2,2),(2,3)}0→7, (1,3)7→8, 1-block→9, 2-block→10.
  const cv::Mat src = matFromRows( { { 8, 0, 7, 0 },
                                     { 0, 8, 0, 7 },
                                     { 1, 1, 0, 0 },
                                     { 1, 1, 2, 2 } } );
  cv::Mat out;
  QString err;
  REQUIRE( RsPostProcess::clump( src, out, 4, &err ) );
  CHECK( out.at<int>( 0, 0 ) == 1 );
  CHECK( out.at<int>( 0, 1 ) == 2 );
  CHECK( out.at<int>( 0, 2 ) == 3 );
  CHECK( out.at<int>( 0, 3 ) == 4 );
  CHECK( out.at<int>( 1, 0 ) == 5 );
  CHECK( out.at<int>( 1, 1 ) == 6 );
  CHECK( out.at<int>( 1, 2 ) == 7 );
  CHECK( out.at<int>( 2, 2 ) == 7 );
  CHECK( out.at<int>( 2, 3 ) == 7 );
  CHECK( out.at<int>( 1, 3 ) == 8 );
  CHECK( out.at<int>( 2, 0 ) == 9 );
  CHECK( out.at<int>( 3, 1 ) == 9 );
  CHECK( out.at<int>( 3, 2 ) == 10 );
  CHECK( out.at<int>( 3, 3 ) == 10 );

  REQUIRE( RsPostProcess::clump( src, out, 8, &err ) );
  // 8-conn: the diagonal 8s merge (id1); ALL 0s merge via diagonals
  // ((0,1)-(1,0)/(1,2), (0,3)-(1,2)) → id2; the diagonal 7s merge → id3;
  // 1-block → id4; 2-block → id5.
  CHECK( out.at<int>( 0, 0 ) == 1 );
  CHECK( out.at<int>( 1, 1 ) == 1 );
  CHECK( out.at<int>( 0, 1 ) == 2 );
  CHECK( out.at<int>( 0, 3 ) == 2 );
  CHECK( out.at<int>( 1, 0 ) == 2 );
  CHECK( out.at<int>( 1, 2 ) == 2 );
  CHECK( out.at<int>( 2, 2 ) == 2 );
  CHECK( out.at<int>( 0, 2 ) == 3 );
  CHECK( out.at<int>( 1, 3 ) == 3 );
  CHECK( out.at<int>( 2, 0 ) == 4 );
  CHECK( out.at<int>( 3, 3 ) == 5 );
}

TEST_CASE( "clump cancellation reports Cancelled",
           "[postprocess][clump][r30t01]" )
{
  cv::Mat src = matFromRows( { { 1, 2 }, { 3, 4 } } );
  cv::Mat out;
  QString err;
  CHECK_FALSE( RsPostProcess::clump( src, out, 4, &err, []() { return true; } ) );
  CHECK( err == QStringLiteral( "Cancelled" ) );
}

TEST_CASE( "clump refuses rasters whose worst-case component count overflows int32",
           "[postprocess][clump][r30t01]" )
{
  // External-data header Mat (no 17 GB allocation; OpenCV 5 refuses a
  // NULL-data Mat, so hand it a real 16-byte buffer it never reads): the
  // guard reads rows/cols only and runs BEFORE any pixel access or type
  // conversion. Worst case EVERY pixel is its own component (a 4-connected
  // checkerboard isolates each pixel), so the honest bound is H*W:
  // 65538² = 4,295,229,444 > INT_MAX-1 = 2,147,483,646.
  static int tinyBacking[4];
  cv::Mat huge( 65538, 65538, CV_32S, static_cast<void *>( tinyBacking ) );
  REQUIRE( huge.rows == 65538 );
  REQUIRE( huge.cols == 65538 );
  cv::Mat out;
  QString err;
  CHECK_FALSE( RsPostProcess::clump( huge, out, 4, &err ) );
  CHECK( err.contains( QStringLiteral( "int32 id range" ) ) );

  // R2 review P3: sieve's refusal path is pinned too (same admission,
  // before its conversion/clone — a huge CV_8U header must refuse typed).
  cv::Mat huge8( 65538, 65538, CV_8U, static_cast<void *>( tinyBacking ) );
  QString sieveErr;
  CHECK_FALSE( RsPostProcess::sieve( huge8, out, 2, 8, &sieveErr ) );
  CHECK( sieveErr.contains( QStringLiteral( "int32 id range" ) ) );

  // The threshold<1 no-op stays permissive (no labeling runs; pure clone of
  // a representable input cannot overflow the id space).
  cv::Mat smallNoOp = matFromRows( { { 1, 2 }, { 3, 4 } } );
  QString noErr;
  CHECK( RsPostProcess::sieve( smallNoOp, out, 0, 8, &noErr ) );
}

TEST_CASE( "clump random equivalence vs connectedComponents per class",
           "[postprocess][clump][r30t01]" )
{
  // The production labeling vs OpenCV per-class CC + raster-order ids.
  std::mt19937 rng( 424242u );
  for ( int iteration = 0; iteration < 40; ++iteration )
  {
    const int H = 5 + int( rng() % 40 );
    const int W = 5 + int( rng() % 40 );
    const int classes = 1 + int( rng() % 5 );
    const int connectedness = ( rng() % 2 ) == 0 ? 4 : 8;
    cv::Mat src( H, W, CV_32S );
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        src.at<int>( r, c ) = int( rng() % ( classes + 1 ) );

    cv::Mat expected( H, W, CV_32S, cv::Scalar( 0 ) );
    int nextId = 1;
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
      {
        if ( expected.at<int>( r, c ) != 0 )
          continue;
        const int classVal = src.at<int>( r, c );
        // BFS with the same connectivity assigning the next id.
        std::vector<std::pair<int, int>> frontier{ { r, c } };
        expected.at<int>( r, c ) = nextId;
        const int dr8[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        const int dc8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        const int dr4[4] = { -1, 1, 0, 0 };
        const int dc4[4] = { 0, 0, -1, 1 };
        const int *dr = connectedness == 8 ? dr8 : dr4;
        const int *dc = connectedness == 8 ? dc8 : dc4;
        const int nDir = connectedness == 8 ? 8 : 4;
        for ( size_t head = 0; head < frontier.size(); ++head )
        {
          const auto [cr, cc] = frontier[head];
          for ( int d = 0; d < nDir; ++d )
          {
            const int nr = cr + dr[d], nc = cc + dc[d];
            if ( nr < 0 || nr >= H || nc < 0 || nc >= W )
              continue;
            if ( expected.at<int>( nr, nc ) != 0 || src.at<int>( nr, nc ) != classVal )
              continue;
            expected.at<int>( nr, nc ) = nextId;
            frontier.push_back( { nr, nc } );
          }
        }
        ++nextId;
      }

    cv::Mat produced;
    QString err;
    REQUIRE( RsPostProcess::clump( src, produced, connectedness, &err ) );
    if ( !matsEqual( expected, produced ) )
    {
      CAPTURE( iteration, H, W, connectedness );
      FAIL( "clump labeling diverged from the per-pixel BFS oracle" );
    }
  }
}
