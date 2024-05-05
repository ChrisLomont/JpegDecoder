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

#if 1
        std::string ws = R"xx([\n\t\r ]*)xx"; // whitespace
        std::string num = R"xx([\n\r\t ]*<rdf:li>([+-]\d+(\.\d+)?)</rdf:li>)xx"; // grouped number
//        std::string item = "GainMapMin";
        std::string pat2 =
            R"xx(<hdrgm:)xx";
        pat2 += 
            item;
        pat2 += R"xx(>)xx";
    	pat2 += ws + R"xx(<rdf:Seq>)xx";
        std::string rdf = R"xx([\n\t\r ]*<rdf:li>(\d+(\.\d+)?)</rdf:li>)xx";
    	//pat2 += R"xx([\n\t\r ]*)xx";
        //pat2 += R"xx(<rdf:li>)xx";
        //pat2 += R"xx((\d+(\.\d+)?))xx";
        //pat2 += R"xx(</rdf:li>)xx";
        pat2 += ws + rdf;
        pat2 += ws + rdf;
        pat2 += ws + rdf;
        pat2 += ws + R"xx(</rdf:Seq>)xx";
        pat2 += ws + R"xx(</hdrgm:)xx";
        pat2 += item + R"xx(>)xx";

        //        pat2 += num + num + num;
//        pat2 += R"([\n\r\t ]*<\/rdf:Seq>[\r\n\t ]*<\/hdrgm:)";
//        pat2 += item;
//        pat2 += R"(>)";
        auto trip = pat2;
        //trip = R"xx(<hdrgm:GainMapMax>)xx";
#else


        std::string ws = "[ \\n]+";
        std::string num = ws+"<rdf:li>([+-0-9\\.]+)</rdf:li>" + ws;
        std::string trip = std::format(
        "<hdrgm:{}>{}<rdf:Seq>{}{}{}</rdf:Seq>{}</hdrgm:{}>",
            item,ws,
            num,num,num,
            ws,item
        );
#endif
        // std::cout << trip << std::endl;
        
    	
        std::string sing = R"xx(hdrgm:)xx";
        sing += item;
        sing += R"xx(="([-+]?\d+(\.\d+)?)")xx";
        
//        auto singleMatch = std::regex_search(text, match, std::regex{ "hdrgm:" + item + "=\"([-+]?\\d+(\\.\\d+)?)\"" });
        auto singleMatch = std::regex_search(text, match, std::regex{ sing });
        std::cout << "HDR Sing " << singleMatch << std::endl;
        auto vectorMatch = singleMatch || std::regex_search(text, match, std::regex{ trip });
        if (singleMatch && match.size()>0)
        {
            // todo - error checking
            std::cout << "HDR SINGLE MATCH size " << match.size() << " val " << match.str(1) << "\n";
            values.push_back(std::stod(match.str(1)));
            return true;
        }
        if (vectorMatch && match.size() == 7)
        {
            values.push_back(std::stod(match.str(1)));
            values.push_back(std::stod(match.str(3)));
            values.push_back(std::stod(match.str(5)));
            std::cout << "HDR VECTOR MATCH size " << match.size() << " val "  << match.str(0) << "\n";
            std::cout << "  vals " << values[0] << ',' << values[1] << ',' << values[2] << std::endl;
            return true;
        }
        if (!required)
        {
            values.push_back(defaultValue);
            return true;
        }
        return false;
    }
};
