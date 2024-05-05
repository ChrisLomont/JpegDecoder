
#include "JpegDecoder.h"

using namespace Lomont::Jpeg;

#include <iostream>
#include <filesystem>
#include <set>

//--------------------------------------------------------------------------//
using namespace ::std;
namespace fs = ::std::filesystem;

void WritePPMs(const std::string& originalFilename, const JpegDecoder& dec)
{ 
    const auto filestem = fs::path(originalFilename).stem().string(); // path/and/filename_stem

    for (size_t i = 0; i < dec.images.size(); ++i)
    {
        string filename = filestem;
        if (dec.images.size() > 0)
            filename += format("_{}", i + 1);
        filename += ".ppm";
        WritePPM(filename, dec.images[i]);

        cout << "Image " << filename << " written\n";
    }
}
void WriteHDRInfo(const std::string& originalFilename, const UltraHdr & hdr)
{
    const auto filestem = fs::path(originalFilename).stem().string(); // path/and/filename_stem
    const string filename = filestem + ".txt";
    ofstream file(filename);
    file << format("HDR info for {}\n", originalFilename);
    hdr.Dump(file);
    file.close();
    cout << "HDR info " << filename << " written\n";
}

void ProcessFiles(
    const string& pathOrFilename,
    bool saveFile = false,
    LogType errMin = LogType::ERROR,
    bool outputErrorsOnly = false
)
{
    set<fs::path> sorted_by_name;

    const string ext = ".jpg";
    if (pathOrFilename.ends_with(ext))
    {
        sorted_by_name.insert(pathOrFilename);
    }
    else {
        for (auto& p : fs::recursive_directory_iterator(pathOrFilename))
        {
            auto ext = p.path().extension();
            if (ext == ".jpg")
                sorted_by_name.insert(p.path());
        }
    }

    cout << format("{} files to check\n", sorted_by_name.size());

    int fileCount = 0, errorCount = 0;

    for (auto& p : sorted_by_name)
    {
        auto fn = p.string();
        JpegDecoder dec;
        dec.logLevel = errMin;
        // we'll override output, so we can block it on errorOnly dumps
        stringstream s;
        dec.output = [&](const string& msg) {s << msg; };
        fileCount++;
        try {
            Decode(fn, dec);
            if (dec.errorCount == 0 && saveFile)
            {
                WritePPMs(fn, dec);
                if (dec.hdr.hasUltraHdr)
                    WriteHDRInfo(fn, dec.hdr);
            }
        }
        catch (exception& e)
        {
            cout << "Exception \n";
            dec.loge("Exception!\n");
        }
        // dump errors only
        if (outputErrorsOnly)
        {
            if (/* dec.warningCount>0 || */ dec.errorCount > 0)
            {
                cout << s.str() << endl;
                errorCount++;
            }
        }
        else
        {
            cout << s.str() << endl;
        }

    }
    cout << format("{} files, {} with errors\n", fileCount, errorCount);
}

void TestReg()
{
//    string pat = R"(<hdrgm:GainMapMin>[\n\t\r ]*<rdf:Seq>([\n\r\t ]*<rdf:li>[+\-0-9\.]+<\/rdf:li>)+[\n\r\t ]*<\/rdf:Seq>[\r\n\t ]*<\/hdrgm:GainMapMin>)";
    string ws = R"([\n\t\r ]*)"; // whitespace
    string num = R"([\n\r\t ]*<rdf:li>([+-]\d+(\.\d+)?)<\/rdf:li>)"; // grouped number
    string item = "GainMapMax";
    string pat2 =
        R"(<hdrgm:)";
    pat2 += item;
	pat2 += R"(>[\n\t\r ]*<rdf:Seq>)";
    pat2 += num + num + num;
    pat2 += R"([\n\r\t ]*<\/rdf:Seq>[\r\n\t ]*<\/hdrgm:)";
    pat2 += item;
    pat2 += R"(>)";

    string txt = 
R"(<hdrgm:GainMapMax>
	<rdf:Seq>
		<rdf:li>-0.07811</rdf:li>
		<rdf:li>-0.049089</rdf:li>
		<rdf:li>-0.028652</rdf:li>
	</rdf:Seq>
</hdrgm:GainMapMax>)";
    smatch sm;
    bool ret = regex_match(txt, sm, regex{pat2});
    cout << format("REG {} {}",ret,sm.size());
    for (auto& s : sm)
        cout << s << endl;
    
}

int main(int argc, char * argv[])
{
   // TestReg();
    //return 0;

    // if name ends in jpg, does one file, else does recursive directory 
    auto processLocation = "jpegtests";

    LogType minLevel = LogType::INFO;
    bool transcodeFile = false; // saves as filename.ppm
    bool dumpOnErrorOnly = false;

    if (argc > 1)
    {
        processLocation = argv[1];
        cout << processLocation << endl;
    }

    // HDR testing
    //processLocation = "HDR/Pixel6-Original.jpg"; // 4080x3072x3 (Y,Cb,Cr), 1020x768x1 (Y) 
    processLocation = "HDR/Pixel6-LR-HDR-1.jpg"; // 4064x3056x3 8 bits (??,Y,Cb), 4064x3056x3 8 bits (??,Y,Cb) (Lightroom 3 channel HDR)

    ProcessFiles(
        processLocation,
        transcodeFile,
        minLevel,
        dumpOnErrorOnly
        );
   
    return 0;
}
    