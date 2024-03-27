#pragma once
#include <vector>
#include <cstdint>
#include "Tiff.h"

namespace Lomont::Jpeg {
	using namespace std;

	// decode MPF data from JPEG (Multi Picture Format, sometimes called MPO)
	// CIPA DC-x007-2009
	// Skia has good notes https://github.com/google/skia/blob/885e8984707ac3309e6aa47be51776dbd623e6a8/src/codec/SkJpegMultiPicture.cpp

	class MpfDecoder : public TiffDecoder
	{
	public:


		bool Decode(Logger& dec, const vector<uint8_t>& data)
		{
			this->dec = &dec;
			this->data = &data;
			
			// reads the structure described in Figure 6 of CIPA DC-x007-2009
			// TIFF, like Exif stuff
			auto ret = TiffDecoder::Decode(dec,data);
			if (!ret)
			{
				dec.loge("MPF data corrupted\n");
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
			dec->logi(format("Mpf has {} entries\n", ifds.size()));
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
			static const std::array<tagDef, 5> tags0 = { {
									{0xB000,	"VersionTag", "u32",1,"Version bytes"},
									{0xB001,	"ImageCount", "u32",1,"4 Version bytes"},
									{0xB002,	"EntryTag", "u32",1,"16 Entry bytes"},
									{0xB003,	"ImageID", "u32",1,"33 ID bytes"},
									{0xB004,	"CapturedFrames", "u32",1,"33 ID bytes"},

							} };
			static const tagDef err = { 0xFFFF,"UNKNOWN MPF","UNKNOWN",-1,"Unknown mpf tag" };
			for (const auto& t : tags0)
			{
				if (t.val == tag)
					return t;
			}
			return err;
		}
	};
}