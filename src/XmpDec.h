#pragma once
#include <vector>
#include <cstdint>
#include <sstream>
#include "HexDump.h"

namespace Lomont::Jpeg {
	using namespace std;

	// decode Exif data from JPEG
	struct Logger;

	class XmpDecoder : Decoder
	{
	public:

		bool Decode(Logger& dec, const vector<uint8_t>& data)
		{
			this->dec = &dec;
			this->data = &data;

			std::stringstream ss;
			HexDump(data.data(), data.size(), ss, 16, 8);
			dec.logi(std::format("XMP data:\n{}",ss.str()));

			return true;

			// 128-bit GUID as 32-byte ASCII string, 0-9A-F
			char guid[32+1];
			for (int i = 0; i < 32; ++i)
				guid[i] = read(1);
			guid[32] = 0;
			int size = read(4);
			dec.logi(format("   XMP GUID is {}, size {}\n", guid, size));


			return true;
		}

	


	private:

	};
}