/***************************************************************************
 * help_topic_text.h — plain-text rendering of a HelpDescriptor
 *
 * The terminal projection of the knowledge base: deterministic,
 * section-structured plain text used by CLI --help-topic / --operator-help.
 * (GUI renders HTML in HelpCenterDialog; agent surfaces use HelpCompact.)
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"

#include <QString>

namespace sicnu::help
{

class HelpTopicText
{
  public:
    /// Full multi-line rendering of one descriptor (no trailing padding).
    static QString render( const HelpDescriptor &descriptor );
};

} // namespace sicnu::help
