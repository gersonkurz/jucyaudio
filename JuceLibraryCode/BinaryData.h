/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   pause_svg;
    const int            pause_svgSize = 254;

    extern const char*   play_arrow_svg;
    const int            play_arrow_svgSize = 190;

    extern const char*   repeat_svg;
    const int            repeat_svgSize = 264;

    extern const char*   repeat_all_svg;
    const int            repeat_all_svgSize = 379;

    extern const char*   repeat_one_svg;
    const int            repeat_one_svgSize = 298;

    extern const char*   shuffle_svg;
    const int            shuffle_svgSize = 272;

    extern const char*   shuffle_on_svg;
    const int            shuffle_on_svgSize = 387;

    extern const char*   stop_svg;
    const int            stop_svgSize = 192;

    extern const char*   volume_down_svg;
    const int            volume_down_svgSize = 270;

    extern const char*   volume_off_svg;
    const int            volume_off_svgSize = 495;

    extern const char*   volume_up_svg;
    const int            volume_up_svgSize = 378;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 11;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
