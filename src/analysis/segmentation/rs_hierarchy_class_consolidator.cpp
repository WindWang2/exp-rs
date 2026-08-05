// rs_hierarchy_class_consolidator.cpp — OBIA Multi-scale class consistency consolidation.
#include "rs_hierarchy_class_consolidator.h"

template <typename T>
static int selectWinningClass( const QMap<int, T> &votes )
{
  int winningClass = -1;
  T maxVotes = static_cast<T>( -1 );
  for ( auto it = votes.constBegin(); it != votes.constEnd(); ++it )
  {
    if ( it.value() > maxVotes )
    {
      maxVotes = it.value();
      winningClass = it.key();
    }
  }
  return winningClass;
}

QMap<int, QMap<quint32, int>> RsHierarchyClassConsolidator::consolidate(
  const RsObjectHierarchy &hierarchy,
  const QMap<int, QMap<quint32, int>> &levelSegmentClasses,
  RsConsolidationMode mode )
{
  QMap<int, QMap<quint32, int>> result = levelSegmentClasses;

  const int numLevels = hierarchy.levelCount();
  if ( numLevels <= 1 || result.isEmpty() )
    return result;

  if ( mode == RsConsolidationMode::BottomUpMajorityVote )
  {
    for ( int lvl = 0; lvl < numLevels - 1; ++lvl )
    {
      const int coarseLvl = lvl + 1;
      const auto &coarseMap = hierarchy.level( coarseLvl );
      const auto coarseSegIds = coarseMap.uniqueLabels();

      for ( quint32 coarseId : coarseSegIds )
      {
        const QVector<quint32> children = hierarchy.childrenOf( coarseLvl, coarseId );
        if ( children.isEmpty() )
          continue;

        QMap<int, int> classVotes;
        for ( quint32 childId : children )
        {
          if ( result[lvl].contains( childId ) )
          {
            int cId = result[lvl][childId];
            classVotes[cId]++;
          }
        }

        int winningClass = selectWinningClass( classVotes );
        if ( winningClass != -1 )
        {
          result[coarseLvl][coarseId] = winningClass;
        }
      }
    }
  }
  else if ( mode == RsConsolidationMode::TopDownInheritance )
  {
    for ( int coarseLvl = numLevels - 1; coarseLvl >= 1; --coarseLvl )
    {
      const int fineLvl = coarseLvl - 1;
      const auto &coarseMap = hierarchy.level( coarseLvl );
      const auto coarseSegIds = coarseMap.uniqueLabels();

      for ( quint32 coarseId : coarseSegIds )
      {
        if ( !result[coarseLvl].contains( coarseId ) )
          continue;

        int parentClass = result[coarseLvl][coarseId];
        const QVector<quint32> children = hierarchy.childrenOf( coarseLvl, coarseId );

        for ( quint32 childId : children )
        {
          result[fineLvl][childId] = parentClass;
        }
      }
    }
  }
  else if ( mode == RsConsolidationMode::ProbabilityWeightedVote )
  {
    for ( int lvl = 0; lvl < numLevels - 1; ++lvl )
    {
      const int coarseLvl = lvl + 1;
      const auto &coarseMap = hierarchy.level( coarseLvl );
      const auto &fineMap = hierarchy.level( lvl );
      const auto coarseSegIds = coarseMap.uniqueLabels();

      for ( quint32 coarseId : coarseSegIds )
      {
        const QVector<quint32> children = hierarchy.childrenOf( coarseLvl, coarseId );
        if ( children.isEmpty() )
          continue;

        QMap<int, double> classWeights;
        for ( quint32 childId : children )
        {
          if ( result[lvl].contains( childId ) )
          {
            int cId = result[lvl][childId];
            double weight = static_cast<double>( fineMap.pixelCount( childId ) );
            classWeights[cId] += weight;
          }
        }

        int winningClass = selectWinningClass( classWeights );
        if ( winningClass != -1 )
        {
          result[coarseLvl][coarseId] = winningClass;
        }
      }
    }
  }

  return result;
}
