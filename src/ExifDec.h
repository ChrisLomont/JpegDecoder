#pragma once
#include <vector>
#include <cstdint>
#include <format>
#include <array>
#include "Tiff.h"

namespace Lomont::Jpeg {
	using namespace std;

	// decode Exif data from JPEG
	struct Logger;

	class ExifDecoder : public TiffDecoder
	{
	public:
		bool Decode(Logger& dec, const vector<uint8_t>& data)
		{
			this->dec = &dec;
			this->data = &data;

			// description https://www.media.mit.edu/pia/Research/deepview/exif.html
			// see also https://www.iptc.org/std-dev/photometadata/documentation/mapping-guidelines/

			// 49492A00 08000000 TIFF header (4949 = Intel order, 4d4d = motorola)
			// 002A = length, always same (could be 2A00 via intel, motorola...)
			// then 4 byte offset to first IFD image (usually value 8)
			//

			//
			// Entries:

			auto ret = TiffDecoder::Decode(dec,data);
			if (!ret)
			{
				dec.loge("Exif data corrupted\n");
				return false;
			}

			return WalkIFD();

		}

	private:
		bool WalkIFD()
		{
			// IFD image file directory
				// - 4 byte number of entries
				// - 12 bytes per entry:
				//   - 2 byte tag
				//   - 2 byte data format
				//   - 4 bytes # of components
				//   - 4 byte offset to data
			dec->logi(format("Exif has {} entries\n", ifds.size()));
			for (auto& ifd : ifds)
			{
				auto t = GetTag(ifd.tag);
				ifd.desc = t.txt;
				ifd.txt = t.desc;

				dec->logi(format("  tag {:02X}, form {}, comp {}, offs {}: {}\n",
					ifd.tag, ifd.form, ifd.count, ifd.offset, ifd.desc
				));
			}
			return true;
		}

		struct tagDef
		{
			int val;
			string txt;
			string form;
			int count;
			string desc;
		};
		const tagDef& GetTag(int tag)
		{
			// todo - data from here, remove copyrightable text https://www.media.mit.edu/pia/Research/deepview/exif.html
			// more from here https://help.accusoft.com/ImagXpress/v12.0/activex/AccusoftImagXpress12~ImagXpress~TagNumber.html
			static const std::array<tagDef, 17> tags0 = { {

						{0x010e,	"ImageDescription	    ", "ascii string       ",-1,"    Describes image"},
						{0x010f,	"Make	                ", "ascii string       ",-1,"    Shows manufacturer of digicam"},
						{0x0110,	"Model	                ", "ascii string       ",-1,"    Shows model number of digicam"},
						{0x0112,	"Orientation	        ", "unsigned short	   ", 1," 	The orientation of the camera relative to the scene, when the image was captured.The start point of stored data is, '1' means upper left, '3' lower right, '6' upper right, '8' lower left, '9' undefined."},
						{0x011a,	"XResolution	        ", "unsigned rational	",1," 	Display / Print resolution of image.Large number of digicam uses 1 / 72inch, but it has no mean because personal computer doesn't use this value to display/print out."},
						{0x011b,	"YResolution	        ", "unsigned rational	",1,""},
						{0x0128,	"ResolutionUnit	        ", "unsigned short	   ", 1," 	Unit of XResolution(0x011a) / YResolution(0x011b). '1' means no - unit, '2' means inch, '3' means centimeter."},
						{0x0131,	"Software	            ", "ascii string       ",-1,"    Shows firmware(internal software of digicam) version number."},
						{0x0132,	"DateTime	            ", "ascii string	   ", 20, "Date / Time of image was last modified.Data format is 'YYYY:MM:DD HH:MM:SS' + 0x00, total 20bytes. In usual, it has the same value of DateTimeOriginal(0x9003)"},
						{0x013e,	"WhitePoint	            ", "unsigned rational	",2," 	Defines chromaticity of white point of the image.If the image uses CIE Standard Illumination D65(known as international standard of 'daylight'), the values are '3127/10000,3290/10000'."},
						{0x013f,	"PrimaryChromaticities	", "unsigned rational	",6," 	Defines chromaticity of the primaries of the image.If the image uses CCIR Recommendation 709 primearies, values are '640/1000,330/1000,300/1000,600/1000,150/1000,0/1000'."},
						{0x0211,	"YCbCrCoefficients	    ", "unsigned rational	",3," 	When image format is YCbCr, this value shows a constant to translate it to RGB format.In usual, values are '0.299/0.587/0.114'."},
						{0x0213,	"YCbCrPositioning	    ", "unsigned short	   ", 1," 	When image format is YCbCr and uses 'Subsampling'(cropping of chroma data, all the digicam do that), defines the chroma sample point of subsampling pixel array. '1' means the center of pixel array, '2' means the datum point."},
						{0x0214,	"ReferenceBlackWhite	", "unsigned rational  ", 6," 	Shows reference value of black point / white point.In case of YCbCr format, first 2 show black / white of Y, next 2 are Cb, last 2 are Cr.In case of RGB format, first 2 show black / white of R, next 2 are G, last 2 are B."},
						{0x8298,	"Copyright	            ", "ascii string       ",-1,"    Shows copyright information"},
						{0x8769,	"ExifOffset	            ", "unsigned long	   ",-1," 	Offset to Exif Sub IFD"},


						{0x13B, "Artist","ascii string",-1,"Artist"}
							} };
			static const tagDef err = { 0xFFFF,"UNKNOWN EXIF","UNKNOWN",-1,"Unknown exif tag" };
			for (const auto& t : tags0)
			{
				if (t.val == tag)
					return t;
			}
			return err;
		}


	};
}