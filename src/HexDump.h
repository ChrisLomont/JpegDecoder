#pragma once
// Chris Lomont
// simple hex dump

#include <ostream>
#include <string>

// buffer in, length in, stream in
// optional width of line, optional splitter
// optional ASCII shown
//
// 000d0: 01 ae 22... 00  |ASCII HERE..|
void HexDump(const uint8_t * data, size_t byteCount, std::ostream & os, size_t bytesPerLine = 16, size_t spacingEveryNBytes = 8)
{
	const auto maxLineCount = (byteCount + (bytesPerLine - 1)) / bytesPerLine; // round up

	os.fill('0');
	for (size_t line = 0; line < maxLineCount; ++line)
	{
		const auto offset = line * bytesPerLine;
		os.width(6); // todo - size based
		os << std::hex << offset << ": ";

		for (size_t i = 0; i < bytesPerLine; ++i)
		{
			if ((i % spacingEveryNBytes) == 0)
			{
				os << ' ';
			}

			const auto address = offset + i;
			if (address < byteCount)
			{
				os.width(2);
				os << static_cast<int>(data[address]) << ' ';
			}
			else
			{
				os << "   ";
			}
		}
		os << '|';
		for (size_t i = 0; i < bytesPerLine; ++i)
		{
			const auto address = offset + i;
			char ch = address < byteCount ? data[address] : ' ';
			if (ch < 32 || 126 < ch)
			{
				ch = '.';
			}
			os << ch;
		}
		os << "\n";
	}
}
