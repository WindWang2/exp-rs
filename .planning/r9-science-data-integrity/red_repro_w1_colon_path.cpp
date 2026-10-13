// red_repro_w1_colon_path.cpp — standalone red reproduction for the #1469
// review finding on radiometric layerLocalFilePath (POSIX colon filenames
// misclassified as provider URIs). Compares the MASTER logic (as shipped at
// 8a0864276) against the POSIX-contract expectations, using the same index
// arithmetic on ASCII vectors (the QString code is pure indexOf comparisons;
// the test vectors are ASCII). Exit code != 0 => the master logic is RED on
// at least one expectation.
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

static const int npos = -1;

static int indexOf(std::string_view s, char c)
{
    const auto i = s.find(c);
    return i == std::string_view::npos ? npos : static_cast<int>(i);
}

// MASTER logic (radiometric_state.cpp @ 8a0864276): drive-letter colon or
// colon-free only; every other colon => provider URI (non-local).
static bool masterIsLocal(std::string_view source)
{
    const int colon = indexOf(source, ':');
    if (colon < 0)
        return true; // POSIX / UNC / relative path
    if (colon != 1 || source.size() < 3)
        return false;
    const char afterColon = source[2];
    const bool firstIsLetter = (source[0] >= 'A' && source[0] <= 'Z')
        || (source[0] >= 'a' && source[0] <= 'z');
    if (!firstIsLetter || (afterColon != '/' && afterColon != '\\'))
        return false;
    return true;
}

// R9 logic: a colon beyond a drive letter is a scheme separator ONLY when it
// precedes every path separator; inside a component (after a separator) it
// is a legal POSIX filename byte.
static bool r9IsLocal(std::string_view source)
{
    if (source.empty())
        return false;
    const int colon = indexOf(source, ':');
    if (colon < 0)
        return true;
    if (colon == 1)
    {
        const bool firstIsLetter = (source[0] >= 'A' && source[0] <= 'Z')
            || (source[0] >= 'a' && source[0] <= 'z');
        if (!firstIsLetter)
            return false;
        return source.size() == 2 || source[2] == '/' || source[2] == '\\';
    }
    if (colon == 0)
        return false;
    const int slash = indexOf(source, '/');
    const int backslash = indexOf(source, '\\');
    const int firstSep = slash < 0 ? backslash : (backslash < 0 ? slash : (slash < backslash ? slash : backslash));
    return firstSep >= 0 && firstSep < colon;
}

struct Case
{
    const char *source;
    bool expectLocal;
};

int main()
{
    const Case cases[] = {
        { "/data/acq:2026.tif", true },
        { "data/acq:2026.tif", true },
        { "//server/share/acq:2026.tif", true },
        { "/plain/path.tif", true },
        { "relative.tif", true },
        { "C:/data/a.tif", true },
        { "C:\\data\\a.tif", true },
        { "/vsimem/acq:2026.tif", true },
        { "https://example.com/a.tif", false },
        { "GTIFF_DIR:1:/data/a.tif", false },
        { "HDF5:\"/data/a.h5\"://ds", false },
        { "WMS:?url=http://x", false },
        { ":/embedded/raster.tif", false },
        { "qrc:/icon.tif", false },
        { "c:name.tif", false },
        { "weird:name.tif", false },
        { "", false },
    };

    int masterFailures = 0, r9Failures = 0;
    for (const Case &c : cases)
    {
        if (masterIsLocal(c.source) != c.expectLocal)
        {
            ++masterFailures;
            std::printf("MASTER RED: %-34s expected local=%d got local=%d\n",
                        c.source, static_cast<int>(c.expectLocal),
                        static_cast<int>(masterIsLocal(c.source)));
        }
        if (r9IsLocal(c.source) != c.expectLocal)
        {
            ++r9Failures;
            std::printf("R9 FAIL:    %-34s expected local=%d got local=%d\n",
                        c.source, static_cast<int>(c.expectLocal),
                        static_cast<int>(r9IsLocal(c.source)));
        }
    }
    std::printf("master failures: %d, r9 failures: %d (of %zu cases)\n",
                masterFailures, r9Failures, sizeof(cases) / sizeof(cases[0]));
    // Red proof: master must fail at least the POSIX colon cases while the
    // R9 rule satisfies the full contract.
    return (masterFailures > 0 && r9Failures == 0) ? 0 : 1;
}
