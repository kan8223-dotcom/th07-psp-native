#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../src/thirdparty/sjis_converter.h"

namespace
{
bool Check(const char *label, const char *input, const char *expected)
{
    char *actual = sjis2utf8(input);
    const bool matches = actual && std::strcmp(actual, expected) == 0;
    if (!matches)
    {
        std::cerr << label << " conversion mismatch\n";
    }
    std::free(actual);
    return matches;
}
} // namespace

int main()
{
    // CP932 0x8160 must be Windows' FULLWIDTH TILDE, not strict-JIS WAVE DASH.
    const char fullwidthTilde[] = {char(0x81), char(0x60), '\0'};
    const char hiraganaA[] = {char(0x82), char(0xa0), '\0'};
    if (!Check("CP932 8160", fullwidthTilde, "\xef\xbd\x9e") ||
        !Check("SJIS 82a0", hiraganaA, "\xe3\x81\x82") ||
        !Check("ASCII", "TH07", "TH07"))
    {
        return 1;
    }
    std::cout << "CP932 wave dash compatibility PASS\n";
    return 0;
}
