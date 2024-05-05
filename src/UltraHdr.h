#pragma once
#include <format>
#include <vector>
#include <regex>

#include "Types.h"

// simple UltraHdr parsing
// handles Google's UltraHdr format details
// https://developer.android.com/media/platform/hdr-image-format

/* GainMap stored in second image as 

<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="XMP Core 5.5.0">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about=""
     xmlns:hdrgm="http://ns.adobe.com/hdr-gain-map/1.0/"
     hdrgm:Version="1.0"
     hdrgm:GainMapMin="-0.57609993"
     hdrgm:GainMapMax="4.7090998"
     hdrgm:Gamma="1"
     hdrgm:OffsetSDR="0.015625"
     hdrgm:OffsetHDR="0.015625"
     hdrgm:HDRCapacityMin="0"
     hdrgm:HDRCapacityMax="4.7090998"
     hdrgm:BaseRenditionIsHDR="False"/>
  </rdf:RDF>
</x:xmpmeta>

*/

class UltraHdr
{
public:
	bool hasUltraHdr{ false };

    double headerVersion{0}; // should be 1.0
    double gainMapMin{ 0 }, gainMapMax{0};
    double gamma{0};
    double offsetSdr{ 0 }, offsetHdr{0};
    double capacityMin{ 0 }, capacityMax{ 0 };
    bool baseRenditionIsHdr{ 0 };

	// feed xmp strings here
	void ParseXmp(Lomont::Jpeg::Logger& dec, const std::vector<uint8_t>& data)
	{
        // nice C++ regex cheatsheet https://cpprocks.com/files/c++11-regex-cheatsheet.pdf

        // parse this
        std::string text(reinterpret_cast<const char*>(data.data()),data.size());

		std::smatch hdrMatch, gainMinMatch, gainMaxMatch, gammaMatch,offsetSDRMatch, offsetHDRMatch,capacityMinMatch, capacityMaxMatch,baseMatch;
        if (
            ParseValue(text, "Version", headerVersion, true)
            && ParseValue(text, "GainMapMin", gainMapMin, false, 0.0)
            && ParseValue(text, "GainMapMax", gainMapMax, true)
            && ParseValue(text, "Gamma", gamma, false, 1.0)

            && ParseValue(text, "OffsetSDR", offsetSdr, false, 1.0/64.0)
            && ParseValue(text, "OffsetHDR", offsetHdr, false, 1.0 / 64.0)
            && ParseValue(text, "HDRCapacityMin", capacityMin, false, 0.0)
            && ParseValue(text, "HDRCapacityMax", capacityMax, true)
            )
        {
            // baseRendition, optional, default false
            std::smatch baseMatch;
            baseRenditionIsHdr =
                std::regex_search(text, baseMatch, std::regex{ "hdrgm:BaseRenditionIsHDR=\"(True|False)\"" })
                && baseMatch.str(1) == "True";

            // validate parameters:
            // todo;

            dec.logi(std::format("UltraHDR detected. Parameters:\n"));
            dec.logi(std::format("   Version       : {}\n",headerVersion));
            dec.logi(std::format("   BaseRenditionIsHDR: {}\n", baseRenditionIsHdr));
            dec.logi(std::format("   GainMapMin    : {}\n", gainMapMin));
            dec.logi(std::format("   GainMapMax    : {}\n", gainMapMax));
            dec.logi(std::format("   Gamma         : {}\n", gamma));
            dec.logi(std::format("   OffsetSDR     : {}\n", offsetSdr));
            dec.logi(std::format("   OffsetHDR     : {}\n", offsetHdr));
            dec.logi(std::format("   HDRCapacityMin: {}\n", capacityMin));
            dec.logi(std::format("   HDRCapacityMax: {}\n", capacityMax));
            hasUltraHdr = true;
        }
	}

    static bool Same(double v1, double v2)
	{
        return fabs(v1 - v2) < 1e-5;
	}

    static bool ParseValue(const std::string& text, const std::string& item, double& value, bool required, double defaultValue = 0.0)
    {
        std::smatch match;
        auto ret = std::regex_search(text, match, std::regex{ "hdrgm:" + item + "=\"(\\d+\\.\\d+)\"" });
        if (ret)
        {
            // todo - error checking
            value = std::stod(match.str(1));
            return true;
        }
        if (!required)
        {
            value = defaultValue;
            return true;
        }
        return false;
    }
};
