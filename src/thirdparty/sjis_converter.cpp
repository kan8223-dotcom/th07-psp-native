#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sjis_conv_table.h"
#include "sjis_converter.h"

char *sjis2utf8(const char *input)
{
    int len = strlen(input);
    // Shift-JIS won't give 4byte UTF8, so max. 3 byte per input char are needed
    char *output = (char *)malloc(3 * len + 1);
    int indexInput = 0, indexOutput = 0;

    while (indexInput < len)
    {
        const uint8_t firstByte = (uint8_t)input[indexInput];
        const char arraySection = firstByte >> 4;
        uint16_t sourceValue = firstByte;

        size_t arrayOffset;
        if (arraySection == 0x8)
            arrayOffset = 0x100; // these are two-byte shiftjis
        else if (arraySection == 0x9)
            arrayOffset = 0x1100;
        else if (arraySection == 0xE)
            arrayOffset = 0x2100;
        else
            arrayOffset = 0; // this is one byte shiftjis

        // determining real array offset
        if (arrayOffset)
        {
            arrayOffset += (firstByte & 0xf) << 8;
            indexInput++;
            if (indexInput >= len)
                break;
            sourceValue = (uint16_t)((firstByte << 8) |
                                     (uint8_t)input[indexInput]);
        }
        arrayOffset += (uint8_t)input[indexInput++];
        arrayOffset <<= 1;

        // unicode number is...
        uint16_t unicodeValue = (shiftJIS_convTable[arrayOffset] << 8) | shiftJIS_convTable[arrayOffset + 1];

        // TH07's Windows assets are CP932, not strict JIS Shift-JIS.  The
        // legacy table maps 0x8160 to U+301C, while Windows/MS Gothic and the
        // local font authority correctly use U+FF5E.  Preserve that original
        // PC rendering without adding a duplicate glyph to the subset.
        if (sourceValue == 0x8160u)
            unicodeValue = 0xff5eu;

        // converting to UTF8
        if (unicodeValue < 0x80)
        {
            output[indexOutput++] = unicodeValue;
        }
        else if (unicodeValue < 0x800)
        {
            output[indexOutput++] = 0xC0 | (unicodeValue >> 6);
            output[indexOutput++] = 0x80 | (unicodeValue & 0x3f);
        }
        else
        {
            output[indexOutput++] = 0xE0 | (unicodeValue >> 12);
            output[indexOutput++] = 0x80 | ((unicodeValue & 0xfff) >> 6);
            output[indexOutput++] = 0x80 | (unicodeValue & 0x3f);
        }
    }

    // remove the unnecessary bytes
    output[indexOutput] = '\0';
    return output;
}
