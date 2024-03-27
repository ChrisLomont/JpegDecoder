#pragma once
#include <vector>
#include <cstdint>

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
			return true;
		}

	private:

	};
}