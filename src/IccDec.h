#pragma once
#include <vector>
#include <cstdint>

namespace Lomont::Jpeg {
	using namespace std;

	// decode ICC data from JPEG
	struct Logger;

	class IccDecoder : Decoder
	{
	public:


		bool Decode(Logger& dec, const vector<uint8_t>& data)
		{
			this->dec = &dec;
			this->data = &data;
			
			// check for ICC profile https://www.color.org/ICC1V42.pdf

			// 128 byte header, then list of tags, all big endian
			// tags zero padded to multiple of 4 bytes

			// read all, check if was valid
			auto size = read(4); // profile size - all tags plus header
			auto cmm = read(4); // preferred CMM
			auto ver = read(4); // version (byte per entry)
			/*
	Input Device profile ‘scnr’ 73636E72h
	Display Device profile ‘mntr’ 6D6E7472h
	Output Device profile ‘prtr’ 70727472h
	DeviceLink profile ‘link’ 6C696E6Bh
	ColorSpace Conversion profile ‘spac’ 73706163h
	Abstract profile ‘abst’ 61627374h
	Named colour profile ‘nmcl’ 6E6D636Ch
			 */
			auto devc = read(4); // device class
			/*
	XYZData ‘XYZ ’ 58595A20h
	labData ‘Lab ’ 4C616220h
	luvData ‘Luv ’ 4C757620h
	YCbCrData ‘YCbr’ 59436272h
	YxyData ‘Yxy ’ 59787920h
	rgbData ‘RGB ’ 52474220h
	grayData ‘GRAY’ 47524159h
	hsvData ‘HSV ’ 48535620h
	hlsData ‘HLS ’ 484C5320h
	cmykData ‘CMYK’ 434D594Bh
	cmyData ‘CMY ’ 434D5920h
	2colourData ‘2CLR’ 32434C52h
	3colourData (if not listed above) ‘3CLR’ 33434C52h
	4colourData (if not listed above) ‘4CLR’ 34434C52h
	5colourData ‘5CLR’ 35434C52h
	6colourData ‘6CLR’ 36434C52h
	7colourData ‘7CLR’ 37434C52h
	8colourData ‘8CLR’ 38434C52h
	9colourData ‘9CLR’ 39434C52h
	10colourData ‘ACLR’ 41434C52h
	11colourData ‘BCLR’ 42434C52h
	12colourData ‘CCLR’ 43434C52h
	13colourData ‘DCLR’ 44434C52h
	14colourData ‘ECLR’ 45434C52h
	15colourData ‘FCLR’ 46434C52h
			 */
			auto inp = read(4); // input color space
			auto pcs = read(4); // Profile connection space (same values as previous)

			read(12); // date - todo - read and parse
			auto ascp = read(4); // profile sig 61637370h
			/*
			Apple Computer, Inc. ‘APPL’ 4150504Ch
			Microsoft Corporation ‘MSFT’ 4D534654h
			Silicon Graphics, Inc. ‘SGI ’ 53474920h
			Sun Microsystems, Inc. ‘SUNW’ 53554E57h
			 */
			auto sig = read(4); // primary platform sig
			auto flags = read(4);
			auto dev = read(4); // device mfr
			auto mod = read(4); // device model
			auto attr1 = read(4); // dev attribs (stuff like reflective, transparency, glossy, polarity, B&W)
			auto attr2 = read(4); // dev attribs
			/*
	Perceptual 0
	Media-Relative Colorimetric 1
	Saturation 2
	ICC-Absolute Colorimetric 3
			 */
			auto intent = read(4);
			auto xyz = readXYZ(); // 12 byte XYZ values, only profile space allowed is illuminant D50 (until new ICC spec)
			auto psig = read(4); // creator profile sig
			read(16); // profile sig
			read(28); // should be 0;

			if (error || readPos != 128 || ascp != 0x61637370)
			{
				dec.loge("ICC profile header error\n");
				return false;
			}

			// Tag Table
			/*
	0 - 3 4 Tag count
	4 - 7 4 Tag Signature
	8 - 11 4 Offset to beginning of tag data element ulnt32Number
	12 - 15 4 Size of tag data element ulnt32Number
	16 - (12n+3) 12n Signature, offset and size respectively of
			 */
			vector<TagHdr> tags;
			auto tagCount = read(4);
			for (int i = 0; i < tagCount; ++i)
			{
				TagHdr h;
				h.sig = read(4);
				h.off = read(4); // relative to 128 byte header
				h.size = read(4);
				tags.push_back(h);
			}

			// tags can be in any order!
			// multiple ones can point to same data!
			// todo - sort increasing offset? else this fails
			for (const auto& t : tags)
			{
				ReadTag(t);
				if (error)
					break;
			}

			//if (readPos != data.size())
			//	error = true;

			return !error;
		}


	private:

		using s15 = int32_t; // signed15Fixed16 number

		struct xyzNumber
		{
			s15 x, y, z;
		};
		// 12 byte XYZ values
		xyzNumber readXYZ()
		{
			xyzNumber n;
			n.x = read(4);
			n.y = read(4);
			n.z = read(4);
			return n;
		}

		struct TagHdr
		{
			uint32_t sig, off, size;
		};

		void ReadTag(const TagHdr& h)
		{
			readPos = h.off;
			//if (h.off != readPos)
			//{
			//	error = true;
			//	return;
			//}
			auto size = (h.size + 3) / 4; // 4 byte align
			read(size * 4);
		}


		bool error{ false };
		uint32_t read(int n)
		{
			uint32_t v = 0;
			int len = n;
			for (int p = 0; p < n; ++p)
			{
				if (readPos >= data->size())
				{
					error = true;
					break;
				}
				uint32_t d = (*data)[readPos++];
				v = 256 * v + d;
			}
			return v;
		}




	};
}