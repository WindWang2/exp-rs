// OBIA Classification Optimization — RsHierarchyClassConsolidator unit test.
#include <catch2/catch_test_macros.hpp>
#include <opencv2/core.hpp>

#include "rs_hierarchy_class_consolidator.h"
#include "rs_object_hierarchy.h"

TEST_CASE( "HierarchyClassConsolidator: BottomUpMajorityVote",
           "[obia][hierarchy][consolidator]" )
{
  RsObjectHierarchy hierarchy;

  QVector<quint32> l0Labels = {
    1, 1, 2, 2,
    1, 1, 2, 2,
    3, 3, 4, 4,
    3, 3, 4, 4
  };
  RsSegmentMap lvl0Map( l0Labels, 4, 4 );

  QVector<quint32> l1Labels = {
    10, 10, 10, 10,
    10, 10, 10, 10,
    20, 20, 20, 20,
    20, 20, 20, 20
  };
  RsSegmentMap lvl1Map( l1Labels, 4, 4 );

  RsParentTable parentTable;
  parentTable.fineToParent[1] = 10;
  parentTable.fineToParent[2] = 10;
  parentTable.fineToParent[3] = 20;
  parentTable.fineToParent[4] = 20;

  hierarchy.setLevels( { lvl0Map, lvl1Map }, { parentTable } );

  QMap<int, QMap<quint32, int>> initialClasses;
  // Fine level 0: child 1 = Water(100), child 2 = Water(100), child 3 = Forest(200), child 4 = Forest(200)
  initialClasses[0][1] = 100;
  initialClasses[0][2] = 100;
  initialClasses[0][3] = 200;
  initialClasses[0][4] = 200;
  // Coarse level 1 initially unassigned / wrong
  initialClasses[1][10] = 0;
  initialClasses[1][20] = 0;

  auto consolidated = RsHierarchyClassConsolidator::consolidate(
      hierarchy, initialClasses, RsConsolidationMode::BottomUpMajorityVote );

  REQUIRE( consolidated[1][10] == 100 );
  REQUIRE( consolidated[1][20] == 200 );
}

TEST_CASE( "HierarchyClassConsolidator: TopDownInheritance",
           "[obia][hierarchy][consolidator]" )
{
  RsObjectHierarchy hierarchy;

  QVector<quint32> l0Labels = {
    1, 1, 2, 2,
    1, 1, 2, 2,
    3, 3, 4, 4,
    3, 3, 4, 4
  };
  RsSegmentMap lvl0Map( l0Labels, 4, 4 );

  QVector<quint32> l1Labels = {
    10, 10, 10, 10,
    10, 10, 10, 10,
    20, 20, 20, 20,
    20, 20, 20, 20
  };
  RsSegmentMap lvl1Map( l1Labels, 4, 4 );

  RsParentTable parentTable;
  parentTable.fineToParent[1] = 10;
  parentTable.fineToParent[2] = 10;
  parentTable.fineToParent[3] = 20;
  parentTable.fineToParent[4] = 20;

  hierarchy.setLevels( { lvl0Map, lvl1Map }, { parentTable } );

  QMap<int, QMap<quint32, int>> initialClasses;
  initialClasses[1][10] = 300; // Urban
  initialClasses[1][20] = 400; // Agriculture

  auto consolidated = RsHierarchyClassConsolidator::consolidate(
      hierarchy, initialClasses, RsConsolidationMode::TopDownInheritance );

  REQUIRE( consolidated[0][1] == 300 );
  REQUIRE( consolidated[0][2] == 300 );
  REQUIRE( consolidated[0][3] == 400 );
  REQUIRE( consolidated[0][4] == 400 );
}

TEST_CASE( "HierarchyClassConsolidator: ProbabilityWeightedVote",
           "[obia][hierarchy][consolidator]" )
{
  RsObjectHierarchy hierarchy;

  QVector<quint32> l0Labels = {
    1, 1, 1, 2,
    1, 1, 1, 2,
    3, 3, 4, 4,
    3, 3, 4, 4
  };
  RsSegmentMap lvl0Map( l0Labels, 4, 4 );

  QVector<quint32> l1Labels = {
    10, 10, 10, 10,
    10, 10, 10, 10,
    20, 20, 20, 20,
    20, 20, 20, 20
  };
  RsSegmentMap lvl1Map( l1Labels, 4, 4 );

  RsParentTable parentTable;
  parentTable.fineToParent[1] = 10;
  parentTable.fineToParent[2] = 10;
  parentTable.fineToParent[3] = 20;
  parentTable.fineToParent[4] = 20;

  hierarchy.setLevels( { lvl0Map, lvl1Map }, { parentTable } );

  QMap<int, QMap<quint32, int>> initialClasses;
  initialClasses[0][1] = 100; // Water (6 pixels)
  initialClasses[0][2] = 200; // Forest (2 pixels)

  auto consolidated = RsHierarchyClassConsolidator::consolidate(
      hierarchy, initialClasses, RsConsolidationMode::ProbabilityWeightedVote );

  REQUIRE( consolidated[1][10] == 100 );
}
