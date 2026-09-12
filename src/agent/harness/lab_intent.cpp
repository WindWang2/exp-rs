// src/agent/harness/lab_intent.cpp
#include "lab_intent.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace sicnu::agent::harness {

namespace {

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

struct IntentSignals
{
  const char *intent;
  int priority;                       ///< tie-break: refusing surfaces win
  std::vector<std::string> signals;   ///< byte substrings, lowercased
};

/// Signal tables per intent, in kLabIntentVocabulary order. Byte substrings:
/// UTF-8 Chinese matches safely; ASCII matches are lowercased on both sides.
/// Refusal default: no signal ⇒ lab_hint.
std::vector<IntentSignals> signalTable()
{
  std::vector<IntentSignals> table;
  table.push_back( { kIntentLabTroubleshoot, 2,
                     { "为什么", "怎么回事", "哪里错", "错了", "不对", "报错",
                       "全是负", "都是负", "全为负", "负数", "全是nodata", "空白",
                       "条纹", "不重合", "kappa" } } );
  table.push_back( { kIntentLabHint, 0,
                     { "怎么做", "怎么操作", "怎么办", "如何做", "下一步", "不会做",
                       "卡住", "卡在", "怎么继续", "提示", "第1步", "第2步", "第3步",
                       "第4步", "stuck", "how do i", "next step" } } );
  table.push_back( { kIntentLabConcept, 1,
                     { "什么是", "是什么意思", "什么意思", "概念", "定义", "解释一下",
                       "解释下", "是指", "什么区别", "what is", "explain" } } );
  table.push_back( { kIntentLabGradeRequest, 4,
                     { "打分", "批改", "评分", "成绩", "批阅", "grade" } } );
  table.push_back( { kIntentLabExecute, 3,
                     { "帮我做", "帮我跑", "帮我算", "帮我执行", "帮我完成", "帮我生成",
                       "帮我交", "替我", "直接给", "直接发", "运行第", "执行第", "跑第",
                       "做完", "代做", "参考答案", "完整参数", "run step", "do it for me",
                       "ignore previous", "give me the result", "finished result" } } );
  for ( auto &entry : table )
    for ( auto &signal : entry.signals )
      signal = lowered( signal );
  return table;
}

} // namespace

LabIntentClassification classifyLabIntent( const std::string &message )
{
  static const std::vector<IntentSignals> kTable = signalTable();
  const std::string loweredMessage = lowered( message );

  LabIntentClassification best;
  best.intent = kIntentLabHint; // refusal default: unknown ⇒ hint, never execute
  int bestScore = 0;
  int bestPriority = -1;

  for ( const IntentSignals &entry : kTable )
  {
    int score = 0;
    LabIntentClassification current;
    current.intent = entry.intent;
    for ( const std::string &signal : entry.signals )
    {
      if ( loweredMessage.find( signal ) != std::string::npos )
      {
        ++score;
        current.matchedSignals.push_back( signal );
      }
    }
    current.score = static_cast<double>( score );
    if ( score == 0 )
      continue;
    if ( score > bestScore || ( score == bestScore && entry.priority > bestPriority ) )
    {
      best = std::move( current );
      bestScore = score;
      bestPriority = entry.priority;
    }
  }
  return best;
}

} // namespace sicnu::agent::harness
