#pragma once
#include <format>
#include <vector>
#include <regex>
#include <ostream>

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
    bool baseRenditionIsHdr{ false };

    // these can be single values for one channel gain maps, or 3 values for RGB gain maps
    std::vector<double> gainMapMin, gainMapMax;
    std::vector<double> gamma;
    std::vector<double> offsetSdr, offsetHdr;
    std::vector<double> capacityMin, capacityMax;

	// feed xmp strings here
	void ParseXmp(Lomont::Jpeg::Logger& dec, const std::vector<uint8_t>& data)
	{
        // nice C++ regex cheatsheet https://cpprocks.com/files/c++11-regex-cheatsheet.pdf

        // parse this
        std::string text(reinterpret_cast<const char*>(data.data()),data.size());

        std::vector<double> headerVersionVec;
		std::smatch hdrMatch, gainMinMatch, gainMaxMatch, gammaMatch,offsetSDRMatch, offsetHDRMatch,capacityMinMatch, capacityMaxMatch,baseMatch;
        if (
            ParseValues(text, "Version", headerVersionVec, true)
            && ParseValues(text, "GainMapMin", gainMapMin, false, 0.0)
            && ParseValues(text, "GainMapMax", gainMapMax, true)
            && ParseValues(text, "Gamma", gamma, false, 1.0)
            && ParseValues(text, "OffsetSDR", offsetSdr, false, 1.0 / 64.0)
            && ParseValues(text, "OffsetHDR", offsetHdr, false, 1.0 / 64.0)
            && ParseValues(text, "HDRCapacityMin", capacityMin, false, 0.0)
            && ParseValues(text, "HDRCapacityMax", capacityMax, true)
            )
        {
            headerVersion = headerVersionVec[0];
            // baseRendition, optional, default false
            std::smatch baseMatch;
            baseRenditionIsHdr =
                std::regex_search(text, baseMatch, std::regex{ "hdrgm:BaseRenditionIsHDR=\"(True|False)\"" })
                && baseMatch.str(1) == "True";

            // validate parameters:
            // todo;

            dec.logi(std::format("UltraHDR detected. Parameters:\n"));
            std::stringstream ss;
            Dump(ss, "   ");
            dec.logi(ss.str());
            hasUltraHdr = true;
        }
	}
    void Dump(std::ostream & os, std::string prefix = "") const
	{
        os << std::format("{}Version       : {}\n", prefix,  headerVersion);
        os << std::format("{}BaseRenditionIsHDR: {}\n", prefix,  baseRenditionIsHdr);
        os << std::format("{}GainMapMin    : {}\n", prefix,  FormatV(gainMapMin));
        os << std::format("{}GainMapMax    : {}\n", prefix, FormatV(gainMapMax));
        os << std::format("{}Gamma         : {}\n", prefix,  FormatV(gamma));
        os << std::format("{}OffsetSDR     : {}\n", prefix,  FormatV(offsetSdr));
        os << std::format("{}OffsetHDR     : {}\n", prefix,  FormatV(offsetHdr));
        os << std::format("{}HDRCapacityMin: {}\n", prefix,  FormatV(capacityMin));
        os << std::format("{}HDRCapacityMax: {}\n", prefix,  FormatV(capacityMax));
    }
private:

    static std::string FormatV(const std::vector<double> & vals)
    {
        std::stringstream ss;
        for (size_t i = 0; i < vals.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << vals[i];
        }
        return ss.str();
    }


    static bool Same(double v1, double v2)
	{
        return fabs(v1 - v2) < 1e-5;
	}

    static bool ParseValues(const std::string& text, const std::string& item, std::vector<double>& values, bool required, double defaultValue = 0.0)
    {
        std::smatch match;

    	// seems google HDR always has '.', Adobe Lightroom skips it for .0
        // google single gain map form: hdrgm:GainMapMin="0.000000"
        // Lightroom 3 channel form   :
        /*
			<hdrgm:GainMapMin>
				<rdf:Seq>
					<rdf:li>-0.07811</rdf:li>
					<rdf:li>-0.049089</rdf:li>
					<rdf:li>-0.028652</rdf:li>
				</rdf:Seq>
			</hdrgm:GainMapMin>
         */

        const std::string num = R"xx((\d+(\.\d+)?))xx"; // grouped number
        const std::string singleRegex = R"xx(hdrgm:)xx" + item + "=\"" + num + "\"";
        const auto singleMatch = std::regex_search(text, match, std::regex{ singleRegex });
        auto vectorMatch = false;
        if (!singleMatch)
        {
            // number in 'li' tag
            const auto linum = Tag("rdf", "li") + num + Tag("rdf", "li", false);

            // open tags
            const std::string tripleRegex =
                // open tags
                Tag("hdrgm", item) + Tag("rdf", "Seq") +
                // add 3 numbers
                linum + linum + linum +
                // close tags
                Tag("rdf", "Seq", false) + Tag("hdrgm", item, false);

            vectorMatch = std::regex_search(text, match, std::regex{ tripleRegex });
        }
        if (singleMatch && match.size()>1)
        {
            // todo - error checking
            values.push_back(std::stod(match.str(1)));
            return true;
        }
        if (vectorMatch && match.size() == 7)
        {
            // todo - error checking
            values.push_back(std::stod(match.str(1)));
            values.push_back(std::stod(match.str(3)));
            values.push_back(std::stod(match.str(5)));
            return true;
        }
        if (!required)
        {
            values.push_back(defaultValue);
            return true;
        }
        return false;
    }

    static std::string Tag(const char * ns, const std::string & tag, bool open=true)
    {
        const std::string ws = R"xx([\n\t\r ]*)xx"; // whitespace
        return ws + (open ? "<" : "</") + ns + ":" + tag + ">";
    }

};
