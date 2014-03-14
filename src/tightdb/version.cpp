
#include <tightdb/version.hpp>

using namespace tightdb;
using namespace std;


string Version::get_version() 
{
    stringstream ss;
    ss << get_major() << "." << get_minor() << "." << get_patch();
    return ss.str();
}

bool Version::is_at_least(int major, int minor, int patch)
{
    if (get_major() < major)
        return false;
    if (get_major() > major)
	return true;

    if (get_minor() < minor)
        return false;
    if (get_minor() > minor)
	return true;

    return (get_patch() >= patch);
}

bool Version::has_feature(Feature feature)
{
    switch (feature) {
        case feature_Debug:
#ifdef TIGHTDB_DEBUG
            return true;
#else
            return false;
#endif

        case feature_Replication:
#ifdef TIGHTDB_ENABLE_REPLICATION
            return true;
#else
            return false;
#endif
    }
    return false;
}
