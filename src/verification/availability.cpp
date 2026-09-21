// availability.cpp — see availability.h for why Missing and Refused differ.

#include "verification/availability.h"

namespace sicnu::verification
{

const char *availabilityToWire( Availability availability )
{
    switch ( availability )
    {
        case Availability::Found:
            return "found";
        case Availability::Missing:
            return "missing";
        case Availability::Refused:
            return "refused";
    }
    return "missing";
}

bool availabilityFromWire( const std::string &wire, Availability &out )
{
    if ( wire == "found" )
    {
        out = Availability::Found;
        return true;
    }
    if ( wire == "missing" )
    {
        out = Availability::Missing;
        return true;
    }
    if ( wire == "refused" )
    {
        out = Availability::Refused;
        return true;
    }
    return false;
}

bool unavailable( Availability availability )
{
    return availability != Availability::Found;
}

} // namespace sicnu::verification
