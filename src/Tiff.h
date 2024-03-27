#pragma once
#include <vector>
#include <cstdint>

#include "Types.h"

namespace Lomont::Jpeg {
	using namespace std;


	// decode Tiff structured data from JPEG
	class TiffDecoder : public Decoder
	{
	public:

		bool Decode(Logger& dec, const vector<uint8_t>& data) override
		{
			this->dec = &dec;
			this->data = &data;

			// 49492A00 08000000 TIFF header (4949 = Intel order, 4d4d = motorola)
			// 002A = length, always same (could be 2A00 via intel, motorola...)
			// then 4 byte offset to first IFD image (usually value 8)
			//

			//
			// Entries:


			if (data.size() < 8)
			{
				return false;
			}
			isIntel = data[0] == 0x49 && data[1] == 0x49;
			isMoto = data[0] == 0x4D && data[1] == 0x4D;
			if (!isIntel && !isMoto)
			{
				return false;
			}
			read(2); // skip endianess
			const auto len = read(2);
			if (len != 0x2A)
			{
				return false;
			}
			const auto ifdOffset = read(4);

			readPos = ifdOffset;
			const auto ret = ParseIFD();
			return ret;
		}

		bool ParseIFD()
		{
			// IFD image file directory
				// - 4 byte number of entries
				// - 12 bytes per entry:
				//   - 2 byte tag
				//   - 2 byte data format
				//   - 4 bytes # of components
				//   - 4 byte offset to data
			const int count = read(2);
			static const string forms[] = { "?","u8","ascii", "u16", "u32", "u a/b", "s8", "undef", "s16", "s32", "s a/b", "f32","f64" };
			for (int n = 0; n < count; ++n)
			{
				ifdDef ifd;
				const auto tag = read(2);
				const auto form = read(2); // 1=u8,2=ascii,3=u16,u32,u rational, s8, undef,s16,s32,s rational,f32,f64
				const auto comp = read(4);
				const auto offs = read(4);

				const auto f = (1 <= form && form <= 12) ? forms[form] : "error";
				ifd.tag = tag;
				ifd.form = f;
				ifd.desc = "";
				ifd.txt = "";
				ifd.count = comp;
				ifd.offset = offs;

				ifds.push_back(ifd);
			}
			return true;
		}

	protected:



		struct ifdDef
		{
			int tag{};
			string form;
			int count{};
			int offset{};

			string txt;
			string desc;
		};
		vector<ifdDef> ifds;


	};
}