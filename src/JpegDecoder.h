#pragma once

// Chris Lomont jpeg decoder 2024
// 
// Notes and more:
// jpeg std https://www.w3.org/Graphics/JPEG/itu-t81.pdf
// JFIF https://www.w3.org/Graphics/JPEG/jfif3.pdf
// jpeg view/dump https://cyber.meme.tips/jpdump/#
// lots of tag info https://exiftool.org/TagNames/JPEG.html
// nice https ://github.com/corkami/formats/blob/master/image/jpeg.md
// JpegSnoop nice - can compare outputs https ://github.com/ImpulseAdventure/JPEGsnoop
// https://exiftool.org/TagNames/JPEG.html

#include <algorithm>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <format>
#include <numbers>
#include <cassert>
#include <cstdint>

// optional decoders
#include "ExifDec.h"
#include "IccDec.h"
#include "XmpDec.h"
#include "MpfDec.h"


namespace Lomont::Jpeg
{


/* todo
  X - done
  1. X move msgs into JpegDecoder
  2. move items into anon namespace
  3. add errors on unsupported things
  4. X warn on things not fully implemented
  5. fuzz across lots of jpegs
  6. huff tree into lower memory - tree is sparse, linear array quite large
  7. add warning if file longer than read, meaning there may be more (or MPF files)
  8. add  Markers seen in a scan to implement
    FFC2 - Progressive DCT - too much work!
    X FFDD - DRI - Define Restart Interval
    FFEC - App marker 12 - ??
    FFEE - App seg 14 - ??
    MPF files
    X - FFFE - Comment - add!
 9. CMYK 4 channel jpeg support
10. Propogate errors out, fail early, on all items
11. Handle large (>65535) sized ICC
12. Move all into JpegDecoder class




 */
    using namespace std;

    struct Image
    {
        vector<uint8_t> data;
        int w, h, channels;
        void Resize(int w1, int h1, int ch)
        {
            w = w1; h = h1; channels = ch;
            data.resize(w * h * 3); // 3 channel RGB
        }
        void Set(int i, int j, int r, int g, int b)
        {
            if (0 <= i && 0 <= j && i < w && j < h)
            {
                auto index = (i + j * w) * 3; // treat as grayscale, RGB
                data[index + 0] = r;
                data[index + 1] = g;
                data[index + 2] = b;
            }
        }
    };



    // huffman table, stored as array
    // root index 1, left of i is 2*i, right is 2*i+1
    // todo - these sparse, make in vector, each node has (value,left,right) index to next in array
    using Tree = vector<short>;

    // tell how channel laid out
    struct ChDef
    {
        int ch; // 1=Y,2=Cb,3=Cr,4=I,5=Q
        int samplingH, samplingV; // 1 = every pixel, 2 = every?
        int qTbl; // quant table
    };


    class JFIFDecoder : public Decoder
    {
    public:
        bool Decode(Logger& dec, const vector<uint8_t>& data)
        {
            this->dec = &dec;
            this->data = &data;
            isMoto = true; // endianess


            auto ver = read(2); // 0x0102
            auto units = read(1); // 0 = none, 1 = dpi, 2 = dots per cm
            auto xDensity = read(2);
            auto yDensity = read(2);
            auto xThumbnail = read(1);
            auto yThumbnail = read(1);
            // n = x*y, then 3n 24 bit RGB
            dec.logi(format(" - JFIF ver {:04X} units {} density {}x{} thumbnail {}x{}\n",
                ver, units, xDensity, yDensity, xThumbnail, yThumbnail
            ))
                ;
            return true;
        }

    };



    // jpeg decoder struct
    struct JpegDecoder : Logger
    {
        JpegDecoder() : image(make_shared<Image>()) {}

        vector<uint8_t> d;
        int offset;
        uint8_t read()
        {
            if (offset >= d.size())
                return 0;
            return d[offset++];
        }
        bool outOfData() const { return offset >= d.size(); }

        // Huffman tables
        Tree tree[2][2]; // 0 = DC, 1 = AC, then 0 = Y, 1 = CbCr
        vector<uint16_t> qtbls[4]; // quantization tables

        shared_ptr<Image> image;

        int lastCode = -1;
        uint16_t seg;

        int channels{ 0 }; // 1 or 3
        ChDef chdefs[4]; // usually 1 or 3 channels, CMYK rare 

        // decode interval, set with FFDD DRI marker, 
        // 0-65535, 0 means unused, can be reset in stream with a DRI of 0
        int decodeInterval{ 0 };
        int marker{ 0 }; // the next marker to find

        // optional decoders
        function<bool(Logger& logger, const vector<uint8_t>& data)> iccDecoder{ nullptr };
        function<bool(Logger& logger, const vector<uint8_t>& data)> exifDecoder{ nullptr };
        function<bool(Logger& logger, const vector<uint8_t>& data)> xmpDecoder{ nullptr };
        function<bool(Logger& logger, const vector<uint8_t>& data)> mpfDecoder{ nullptr };

    };


    uint16_t read2(JpegDecoder& dec)
    {
        const auto b1 = dec.read();
        const auto b2 = dec.read();
        return static_cast<uint16_t>(256 * b1 + b2);
    }

    // quant table
    bool DecodeDQT(JpegDecoder& dec)
    {
        auto len = read2(dec);
        len -= 2;

        // 2 QT tables, one for luminance, one for chrominance
        //offset += 64*(prec/2 + 1); // bytes // QT values

        // parse quant table(s)
        for (int qt = 0; qt < len / 65; ++qt)
        {
            uint8_t b = dec.read(); // describe table
            int numQT = b & 15;       // 0-3, else error
            int prec = (b >> 4);      //*8+8; // 0 -> 8 bit, else 16 bit


            auto& q = dec.qtbls[numQT & 3];

            for (int i = 0; i < 64; i++)
                q.push_back(dec.read());
        }
        return true;
    }



    // store binary tree in array, root index 1
    // left from i is 2*i, right from i is 2*i+1
#define ROOT 1
#define LEFT(k) (2*(k))
#define RIGHT(k) (2*(k)+1)
#define TOUCH(k) tree[(k)] = -2 // used, no value
#define USED(k) (tree[(k)] != -1) // unused
#define KIDS(k) TOUCH(LEFT(k)); TOUCH(RIGHT(k))
#define PARENT(n) ((n)/2)

// get rightmost node on level
// return -1 if off tree
    int RightLevelNode(const Tree& tree, int n)
    {
        if (n <= 0 || !USED(n)) return -1;

        if (n != ROOT && LEFT(PARENT(n)) == n)
            return RIGHT(PARENT(n));

        // else walk up tree
        int count = 0;
        while (n != ROOT && RIGHT(PARENT(n)) == n)
        {
            n = PARENT(n);
            count++;
        }
        if (n == ROOT) return -1; // at root, end of search

        n = RIGHT(PARENT(n));

        while (count > 0)
        {
            n = LEFT(n);
            count--;
        }

        return n;
    }

    void DumpTree(const Tree& tree, int n = ROOT, int depth = 0)
    {
        for (n = ROOT; n < tree.size(); ++n)
        {
            if (!USED(n))
                continue;
            auto left = LEFT(n), right = RIGHT(n);
            auto lv = (left < tree.size()) ? tree[left] : -100;
            auto rv = (right < tree.size()) ? tree[right] : -100;
            cout << format("Node {} left {}={}, right {}={}\n", n, left, lv, right, rv);
        }
    }
    // match JpegSnoop
    void TreeLengths(const Tree& tree, vector<vector<int>>& data, int n = ROOT, int depth = 0)
    {
        if (tree[n] != -2)
            data[depth].push_back(tree[n]);
        auto left = LEFT(n), right = RIGHT(n);
        if (left < tree.size() && USED(left))
            TreeLengths(tree, data, left, depth + 1);
        if (right < tree.size() && USED(right))
            TreeLengths(tree, data, right, depth + 1);
    }

    // dump codes of each length
    void DumpTree2(const Tree& tree, JpegDecoder& dec)
    {
        vector<vector<int>> data(17); // for each bit length 1-16, put codes here
        TreeLengths(tree, data);
        for (int len = 1; len <= 16; ++len)
        {

            auto cnt = len < data.size() ? data[len].size() : 0;
            auto s = format("   Codes of length {} bits ({} total):", len, cnt);

            if (len < data.size())
            {
                for (auto v : data[len])
                {
                    s += format(" {:02X}", v);
                }
            }
            s += "\n";
            dec.logv(s);
        }
    }


    // huffman: 16 bytes, nth each is count of length n symbols to follow, then symbols
    // return length
    // NOTE: one DHT can have multiple Huffman tables!
    // this is not in the online tutorials on JPEG decoders :|
    // https://stackoverflow.com/questions/64994547/fewer-than-4-huffman-tables-in-a-jpeg-file
    bool DecodeDHT(JpegDecoder& dec)
    {
        int offset = dec.offset;
        auto len1 = read2(dec);

        while (dec.offset - offset < len1)
        {
            // 4 bit fields identify AC (1) or DC (0) and numeric id for table (0 or 1, 0=Y, 1 = color)
            // then 16 bytes for # of each length, then that many symbols (?)
            uint8_t b = dec.read();
            int numHT = b & 7; // 0-3 used, else error
            int ACDC = (b >> 4) & 1; // 0 = DC, 1 = AC
            // bits 5-7 should be 0
            dec.logv(format("DHT: ac {} num {}\n", ACDC, numHT));
            auto& tree = dec.tree[ACDC][numHT];

            int len[16] = { 0 }, sum = 0, max = 0;
            for (int i = 0; i < 16; i++)
            {
                len[i] = dec.read();
                sum += len[i];
                if (len[i] > 0)
                    max = i + 1;
            }

            tree.resize((1 << (max + 1)) + 1); // possible 2^max present nodes, +1 for 1 indexed
            for (int j = 0; j < tree.size(); ++j)
            {
                tree[j] = -1; // unused
            }
            TOUCH(ROOT); // root
            KIDS(ROOT); // and children
            int leftmost = LEFT(ROOT);

            for (int i = 0; i < max; ++i)
            {
                if (len[i] == 0)
                {
                    auto current = leftmost;
                    while (current > 0 && USED(current))
                    {
                        KIDS(current);
                        current = RightLevelNode(tree, current);
                    }
                    leftmost = LEFT(leftmost);
                }
                else
                {
                    for (int j = 0; j < len[i]; ++j)
                    {
                        tree[leftmost] = dec.read(); // 0-255
                        leftmost = RightLevelNode(tree, leftmost);
                    }
                    if (i != max - 1) // else done
                    {
                        KIDS(leftmost);
                        auto current = RightLevelNode(tree, leftmost);
                        leftmost = LEFT(leftmost);
                        while (current >= ROOT && USED(current))
                        {
                            KIDS(current);
                            current = RightLevelNode(tree, current);
                        }
                    }
                }

                //            cout << "Depth " << i << endl;
            }
            //DumpTree(tree);
            DumpTree2(tree, dec);
        }
        return true;
    }


    bool DecodeSOF(JpegDecoder& dec)
    {
        auto len = read2(dec);

        auto bitsPerSample = dec.read(); // 8 bits, 12 and 16 not well supported
        int h = read2(dec); // pixel size
        int w = read2(dec);
        int channels = dec.read(); // 1 = gray, 3 = YCbCr or YIQ, 4 = CMYK rare
        dec.image->Resize(w, h, channels);
        dec.logi(format("SOF: {}x{} {} channels, {} bits/sample\n", w, h, channels, bitsPerSample));
        if (dec.channels == 4)
            dec.loge("4 channel CMYK JPEG not supported\n");

        dec.channels = channels;

        for (int k = 0; k < channels; ++k)
        {
            static string chans[] = { "??","Y","Cb","Cr","I","","Q" };
            auto t1 = dec.read(); // 1=Y,2=Cb,3=Cr,4=I,5=Q
            auto t2 = dec.read(); // sampling factors, 4 bits each
            auto t3 = dec.read(); // quant table number
            dec.chdefs[k].ch = t1;
            dec.chdefs[k].samplingH = t2 >> 4;
            dec.chdefs[k].samplingV = t2 & 15;
            dec.chdefs[k].qTbl = t3;
            string ch = "";
            ch += t1;
            if (dec.channels != 4)
                ch = chans[t1];
            assert(dec.channels == 4 || k == 0 || (t2 == 0x11)); // all chroma forms allowed look like nxn, 1x1, 1x1
            //   assert(dec.channels == 4 ||  dec.chdefs[k].samplingH == dec.chdefs[k].samplingV);// always true?
            dec.logi(format("  - {}: {} sampling {}x{} qtbl {}\n", k, ch, (t2 >> 4), (t2 & 15), t3));
        }
        /* usual
         else decoder needs subsampling
        SOF: 405x405 3 channels, 8 bits/sample
              - 0: Y sampling 1x1 qtbl 0
              - 1: Cb sampling 1x1 qtbl 1
              - 2: Cr sampling 1x1 qtbl 1
         */

        return dec.channels == 1 || dec.channels == 3; // disallow CMYK for now
    }

    // decode 8x8 block into decoding buffer
    // performs inverse DCT and stores float values
    void InvertDCT(const int block[8][8], vector<double>& buffer, int mcuX, int mcuY, int mcuXMax)
    {
        const double invsqrt2 = 1.0 / sqrt(2.0);

        // where in buffer results go 
        // on 8x8 tiled boundaries
        const int bx = mcuX * 8;
        const int by = mcuY * 8;
        const int bstride = mcuXMax * 8;

        for (auto x = 0; x < 8; ++x)
            for (auto y = 0; y < 8; ++y)
            {
                {

                    double sum = 0.0;
                    for (int u = 0; u < 8; ++u)
                    {
                        double Cu = u == 0 ? invsqrt2 : 1.0;
                        double one = 0.0;
                        for (int v = 0; v < 8; ++v)
                        {
                            double Cv = v == 0 ? invsqrt2 : 1.0;
                            one += Cv * block[u][v] * cos((2 * y + 1) * v * numbers::pi / 16.0);
                        }
                        sum += Cu * cos((2 * x + 1) * u * numbers::pi / 16.0) * one;
                    }
                    double value = 0.25 * sum;

                    // here we transpose the 8x8 block
                    int dest = (y + bx) + (x + by) * bstride;
                    buffer[dest] = value;
                }
            }
    }

    // decode MCU into final pixels
    void DecodeMCU(
        const vector<vector<double>>& buffers,
        Image& img,
        int destX, int destY, // where to output in final image
        int srcW, int srcH, // src size
        int hi[4], int vi[4], // per component scalings
        int hmax, int vmax,
        int channels
    )
    {
        auto ReadPlane = [&](int p, int x, int y)
            {
                int sx = (x * hi[p]) / hmax;
                int sy = (y * vi[p]) / vmax;
                int index = sx + sy * hi[p] * 8;
                return buffers[p][index];
            };

        // iterate over src size
        for (auto y = 0; y < srcH; ++y)
            for (auto x = 0; x < srcW; ++x)
            {
                // get Y,Cb,Cr from (possibly) varied sized buffers
                // do level shifts (others merged into YCbCr->RGB)
                double Y = ReadPlane(0, x, y) + 128;
                double Cb = channels >= 3 ? ReadPlane(1, x, y) : 0.0;
                double Cr = channels >= 3 ? ReadPlane(2, x, y) : 0.0;

                // YCbCr -> RGB, CCIR 601, NO GAMMA!
                int R = static_cast<int>(std::round(Y + 1.402 * Cr));
                int G = static_cast<int>(std::round(Y - 0.344136 * Cb - 0.714136 * Cr));
                int B = static_cast<int>(std::round(Y + 1.772 * Cb));

                R = std::clamp(R, 0, 255);
                G = std::clamp(G, 0, 255);
                B = std::clamp(B, 0, 255);
                img.Set(x + destX, y + destY, R, G, B);
            }
    }

    struct BitReader
    {
        int bitPos{ 0 }; // bits left in byte
        uint8_t byte;
        int lastCode{ -1 };
        JpegDecoder* dec{ nullptr };
        bool done{ false };

        // read till next marker
        // return if successful
        bool readMarker(int markerIndex)
        {
            // https://stackoverflow.com/questions/8748671/jpeg-restart-markers

            auto marker = 0xFFD0 + markerIndex;

            dec->logv(format("Seeking reset marker {}...", markerIndex));
            // flush out current byte
            while (bitPos > 0)
                readOne();

            // read till code passed
            bool found = false;
            while (!found && !dec->outOfData())
            {
                int b = -1;
                do { b = dec->read(); } while (b != 0xFF && !dec->outOfData());
                if (b == 0xFF && !dec->outOfData())
                {
                    int b = dec->read();
                    found = b == 0xD0 + markerIndex;

                }

            }
            if (found)
            {
                dec->logv("found\n");
                return true;
            }
            else
            {
                dec->logi("did not find!\n");
                return false;
            }
        }

        int readOne()
        {
            if (done) return 0;
            ++k;
            if (bitPos > 0)
            {
                auto bit = (byte >> 7) & 1;
                byte <<= 1;
                bitPos--;
                return bit;
            }

            // get new byte
            int b = dec->read();
            // byte stuffing should follow any 0xFF with 0x00, if not, we're done (and should have read the 0xFFDA end marker)
            if (b == 0xff)
            {
                int nxt = dec->read();
                if (nxt != 00)
                {
                    lastCode = 0xFF00 + nxt;
                    dec->loge(format("0x{:04X} token in compressed decode, unsupported\n", lastCode));
                    done = true;
                    return 0;
                }
            }
            byte = b;
            bitPos = 8;
            return readOne();

        }


        int k{ 0 };
        string scanData;

        int next1() {
            return readOne();
        }

        int read1(int bitLen)
        {
            if (bitLen == 0) return 0;
            const auto sgn = next1();
            auto value = 1 << (--bitLen);
            while (bitLen-- > 0)
            {
                auto d = next1();
                if (d == sgn)
                    value += 1 << bitLen;
            }
            return (sgn == 0 ? -1 : 1) * value;
        }
    };

    // return -1 for EOB, else the value
    int DecodeHuffman(BitReader& br, const auto& tree)
    {

        int symLen = 0;
        int n = ROOT; // root
        int val = 0;
        while (true)
        {
            const auto bit = br.next1();
            symLen++;
            val = (val << 1) + bit;

            if (bit == 0)
                n = LEFT(n);
            else
                n = RIGHT(n);

            if (n >= tree.size())
            {
                cout << format("FATAL bitlen {} val {:04X}\n", symLen, val);
            }
            if (USED(n) && tree[n] >= 0)
            {
                if (tree[n] == 0x0000)
                    return -1;
                return tree[n];
            }
        }
    }


#undef PARENT
#undef KIDS
#undef USED
#undef TOUCH
#undef RIGHT
#undef LEFT

    void DumpPrefix(JpegDecoder& dec, const vector<uint8_t>& buffer)
    {
        int len = buffer.size();
        if (len > 50) len = 50;
        cout << "Data: ";
        for (auto i = 0; i < len; ++i)
        {
            char c = buffer[i];
            if (c < 0) c = '.';
            if (c < 32 || 127 < c) c = '.';
            cout << c;
        }
        cout << endl;
    }


    // decode compressed data
    void DecodeImg(JpegDecoder& dec)
    {
        BitReader br;

        br.dec = &dec;

        // to decode the possibly different sampling rates of
        // the chroma subsampling, this section notation follows
        // the Jpeg spec, Annex A



        // sampling stuff in dec sets chroma subsampling
    // https://zpl.fi/chroma-subsampling-and-jpeg-sampling-factors/
    //
    /* J:a:b    H   V   Sampling factors (Y Cb Cr)
       4:4:4            1x1,1x1,1x1
       4:4:0            1x2,1x1,1x1
       4:2:2            2x1,1x1,1x1
       4:2:0            2x2,1x1,1x1
       4:1:1            4x1,1x1,1x1
       4:1:0            4x2,1x1,1x1

    Note there are other ways to specify these, but non-standard, and breaks things
    ex: 4:4:4 = 3x1,3x1,3x1 ok!
    Also some really weird ones
    3:1:0 = 3x2,1x1,1x1
      ??? = 1x4,1x3,1x3

    see standard p 24, 36-37
    https://www.w3.org/Graphics/JPEG/itu-t81.pdf

        */
        // for chroma subsampling, we'll decode to float arrays of various sizes, to store the Y Cb Cr channels
    // after all decoded, we'll convert to 8 bit RGB


        // max sampling factors
        int hmax = 1, vmax = 1;
        int prod = 1;
        for (int i = 0; i < dec.channels; ++i)
        {
            hmax = max(hmax, dec.chdefs[i].samplingH);
            vmax = max(vmax, dec.chdefs[i].samplingV);
            prod += dec.chdefs[i].samplingH * dec.chdefs[i].samplingV;
        }
        assert(prod <= 10); // Jpeg requirement

        // MCU pixel width is 8 * hmax, so we want total image width X to be a multiple of this
        // compute based on required imge, round up to multiple of 8*hmax, then scale back to pixels
        const int X = (8 * hmax) * ((dec.image->w + 8 * hmax - 1) / (8 * hmax));
        // similarly...
        const int Y = (8 * vmax) * ((dec.image->h + 8 * vmax - 1) / (8 * vmax));


        int xi[4], yi[4]; // pixel size of ith component
        int hi[4], vi[4]; // sampling sizes of ith component
        vector<vector<double>> buffers(4); // one buffer per component, used to hold one MCU

        for (int i = 0; i < dec.channels; ++i)
        {
            hi[i] = dec.chdefs[i].samplingH;
            vi[i] = dec.chdefs[i].samplingV;

            xi[i] = (X * hi[i] + hmax - 1) / hmax; // rounded up pixel size
            yi[i] = (Y * vi[i] + vmax - 1) / vmax; // 

            buffers[i].resize(hi[i] * vi[i] * 64); // 8x8 per sampling block
        }




        // The pixel arrays for the decoded DCT coeffs
        int run[64];


        // running DC offsets, used as deltas per MCU block
        int lastDC[4] = { 0,0,0 };

        const int mcuMaxH = (X / 8) / hmax, mcuMaxV = (Y / 8) / vmax;

        const int mcuCount = mcuMaxH * mcuMaxV;

        // component ordering in jpeg spec, A.2.3
        // a Minimum Coded Unit is a set of 8x8 blocks that make a minimal
        // size for the various sample sizes (helps minimize mem requirements for decoding)
        // we are ugly here and simply decode everything into RAM, then reconstruct

        // todo  clean function, make comments match code.

        int restartInterval = dec.decodeInterval;

        for (auto mcuY = 0; mcuY < mcuMaxV; ++mcuY)
            for (auto mcuX = 0; mcuX < mcuMaxH; ++mcuX)
            {
                const auto mcuIndex = mcuX + mcuY * mcuMaxH;

                dec.logv(format("Decoding MCU-{}/{}\n", mcuIndex + 1, mcuCount));
                for (auto compID = 0; compID < dec.channels; ++compID)
                {

                    // MCU decode for this channel
                    for (auto mcuY = 0; mcuY < vi[compID]; ++mcuY)
                        for (auto mcuX = 0; mcuX < hi[compID]; ++mcuX)
                        {

                            // decode 1 DC and 63 AC coeffs

                            // zero block
                            for (int j = 0; j < 64; ++j)
                                run[j] = 0;

                            int huffTbl = compID == 0 ? 0 : 1;

                            int coeffCount = 0;

                            while (coeffCount < 64)
                            {
                                // HT or AC coeffs
                                auto acdc = coeffCount == 0 ? 0 : 1; // 0 = DC, 1 = AC

                                auto value = DecodeHuffman(br, dec.tree[acdc][huffTbl]);

                                if (value != -1)
                                {
                                    int zeroCount = (uint8_t)(value) >> 4;
                                    int bitLen = (uint8_t)(value) & 0x0F;

                                    int coeff = br.read1(bitLen);

                                    coeffCount += zeroCount; // append zeros

                                    run[coeffCount] = coeff;
                                } // else default 0
                                else if (coeffCount != 0)
                                {
                                    break; // done
                                }
                                ++coeffCount;
                            }



                            // DC_i = DC_i-1 + DC-difference
                            lastDC[compID] += run[0];
                            run[0] = lastDC[compID];

                            // zig-zag order (i,j) as nibbles
                            static const int zigzagOrder[64] =
                            {
                                0x00, 0x01, 0x10, 0x20, 0x11, 0x02, 0x03, 0x12, 0x21, 0x30, 0x40, 0x31, 0x22, 0x13, 0x04, 0x05,
                                0x14, 0x23, 0x32, 0x41, 0x50, 0x60, 0x51, 0x42, 0x33, 0x24, 0x15, 0x06, 0x07, 0x16, 0x25, 0x34,
                                0x43, 0x52, 0x61, 0x70, 0x71, 0x62, 0x53, 0x44, 0x35, 0x26, 0x17, 0x27, 0x36, 0x45, 0x54, 0x63,
                                0x72, 0x73, 0x64, 0x55, 0x46, 0x37, 0x47, 0x56, 0x65, 0x74, 0x75, 0x66, 0x57, 0x67, 0x76, 0x77
                            };

                            // apply quantize, de-zigzag
                            int block[8][8];
                            const auto& qTbl = dec.qtbls[dec.chdefs[compID].qTbl];
                            //   compID == 0 ? 0 : 1];
                            for (int k = 0; k < 64; ++k)
                            {
                                const auto coords = zigzagOrder[k];
                                block[coords >> 4][coords & 0xF] = run[k] * qTbl[k];
                            }

                            // invert 8x8 DCT block into real valued MCU component buffer
                            InvertDCT(block, buffers[compID], mcuX, mcuY, hi[compID]);
                        } // MCU x and y units 

                } // components


                int destX = mcuX * hmax * 8, destY = mcuY * vmax * 8;

                // decode MCU into final pixels
                DecodeMCU(
                    buffers,
                    *(dec.image),
                    destX, destY,
                    hmax * 8, vmax * 8,
                    hi, vi,
                    hmax, vmax,
                    dec.channels
                );

                // restart markers?
                if (restartInterval && mcuIndex != mcuMaxH * mcuMaxV - 1)
                {
                    if (--restartInterval == 0)
                    {
                        restartInterval = dec.decodeInterval;

                        auto found = br.readMarker(dec.marker);
                        if (!found)
                        {
                            dec.loge(format("Error trying to get restart marker {} at MCU {} out of {} MCUs, step size {}\n",
                                dec.marker, mcuIndex, mcuMaxH * mcuMaxV, dec.decodeInterval
                            ));
                            return;

                        }
                        // todo - ensure order right, correct spacing, make robust?
                        dec.marker = (dec.marker + 1) & 7;
                        lastDC[0] = 0;
                        lastDC[1] = 0;
                        lastDC[2] = 0;
                    }
                }

            } // end of all MCU decoded




            // The remaining bits, if any, in the scan data are discarded as
            // they're added byte align the scan data.

        auto bitsLeft = (8 - br.bitPos) & 7;
        dec.logv(format("Decode finished, {} bits left over", bitsLeft));

        dec.lastCode = br.lastCode;
    }

    bool DecodeSOS(JpegDecoder& dec)
    { // tells which huffman tables used for which parts of decode
        auto len = read2(dec);
        dec.lastCode = -1;

        int numCom = 0; // # of components

        numCom = dec.read(); // must be 1-4
        for (auto i = 0; i < numCom; ++i)
        {
            int comInfo = read2(dec); // component id and huffman tbl used
            uint8_t cID = comInfo >> 8; // 1st byte is component id
         //   assert(numCom == 4 || cID == i + 1);
            if (numCom != 4 && cID != i+1)
                dec.logw(format("Weird #components {} and cID {} entries in SOS\n",numCom,cID));


            int dcNum = (comInfo >> 4) & 15; // should be 0-3 (is 0-1 for baseline jpeg)
            int acNum = (comInfo) & 15;    // should be 0-3 (is 0-1 for baseline jpeg)
            int acdc = i == 0 ? 0 : 1;
           // assert(numCom == 4 || (dcNum == acdc && acNum == acdc));
            if (numCom != 4 && (dcNum != acdc || acNum != acdc))
                dec.logw("Weird ac,dc entries in SOS\n");
            dec.logi(format("SOS {}: cid {} ac {} dc {}\n", i, cID, acNum, dcNum));
        }
        // skip 3
        auto ss = dec.read(); // Ss - where to put first DC coeff, should be 0 in baseline
        auto se = dec.read(); // Se - last DC coeff in block, should be 63 in baseline
        auto bp = dec.read(); // Ah,Al - bit approximation stuff, should be 0,0 in baseline
        if (ss != 0 || se != 64 || bp != 0)
            dec.logw("Weird skip entries in SOS\n");
        //assert(ss == 0 && se == 63 && bp == 0);

        DecodeImg(dec);

        // todo ?  assert(dec.lastCode == 0xFFDA); // else something else, maybe a restart (not impl yet)

        return dec.lastCode == 0xFFDA; // end file
    }

    // try to detect a specific Application extension
    // look for bytes in header, if matches, rest copied into data
    bool DecodeApp(JpegDecoder& dec, const string& header, const vector<uint8_t>& input, vector<uint8_t>& data)
    {
        int i = 0; // look over profile
        bool hasPrefix = true;
        int hlen = header.size();
        for (auto c : input)
        {
            if (i < hlen)
                hasPrefix &= header[i] == c;
            else if (hasPrefix)
                data.push_back(c);
            ++i;
        }
        return hasPrefix;
    }
    //read segment into buffer (not including length)
    bool ReadSegment(JpegDecoder& dec, vector<uint8_t>& data)
    {
        uint16_t len = read2(dec);
        if (len >= 2) len -= 2;

        while (len-- > 0)
        {
            data.push_back(dec.read());
        }
        return true;

    }

    bool DecodeApp0(JpegDecoder& dec)
    {
        vector<uint8_t> data, input;
        ReadSegment(dec, input);
        auto hasJFIF = DecodeApp(dec, "JFIF\0"s, input, data);

        if (hasJFIF)
        {
            dec.logi(format("APP-0: Has JFIF info of length {}\n", data.size()));
            JFIFDecoder jf;
            jf.Decode(dec,data);

        }
        else
            dec.loge(format("Unsupported marker APP-0\n"));
        return true;
    }

    bool DecodeApp1(JpegDecoder& dec)
    {
        // exif https://www.kodak.com/global/plugins/acrobat/en/service/digCam/exifStandard2.pdf
        vector<uint8_t> data, input;
        ReadSegment(dec, input);

        bool hasAd = false;;

        string exifHeader = "Exif\0\0"s; // use C++ string literal with 2 embedded nulls
        string ans = "http://ns.adobe.com/xmp/extension/\0"s;

        // XMP? https://www.adobe.com/products/xmp.html, https://stackoverflow.com/questions/23253281/reading-jpg-files-xmp-metadata
         // https://github.com/adobe/XMP-Toolkit-SDK/blob/main/docs/XMPSpecificationPart3.pdf

        bool success = true;
        if (DecodeApp(dec, exifHeader, input, data))
        {
            dec.logi(format("APP-1: Has EXIF info of length {}\n", data.size()));
            if (dec.exifDecoder)
            {
                success = dec.exifDecoder(dec, data);
            }
        }
        else if (DecodeApp(dec, "http://ns.adobe.com/xap/1.0/", input, data))
        {
            dec.logi(format("APP-1: Has XMP info of length {}\n", data.size()));
            if (dec.xmpDecoder)
            {
                success = dec.xmpDecoder(dec, data);
            }
        }
        else if (DecodeApp(dec, ans, input, data))
        {
            dec.logi(format("APP-1: Has Adobe info of length {}\n", data.size()));
            dec.logw("  - Adobe format not supported\n");
        }
        else {
            dec.loge(format("Unsupported marker APP-1\n"));
            DumpPrefix(dec, input);
        }

        return true;
    }

    bool DecodeApp2(JpegDecoder& dec)
    {
        // 2 byte length
        // check for ICC profile https://www.color.org/ICC1V42.pdf
        // null term string "ICC_PROFILE"
        // byte chunk count, started at 1
        // byte total number of chunks
        // all chunks same length
        //

        vector<uint8_t> data, input;
        ReadSegment(dec, input);
        string iccHeader = "ICC_PROFILE\0"s; // use C++ string literal with embedded nulls

        // MultiPicture format?
        string mpHeader = "MPF\0"s;

        // FlashPix format?
        // https://graphcomp.com/info/specs/livepicture/fpx.pdf
        string fpxrHeader = "FPXR"s;

        if (DecodeApp(dec, iccHeader, input, data) && data.size() >= 2)
        {
            // read 2 bytes: 1st is 1 indexed chunk #, 2nd is # of chunks, we support both being 1
            int chunk = data[0], count = data[1];
            dec.logi(format("APP-2: Has ICC profile of length {}, chunk {}/{}\n", data.size(), chunk, count));
            if (dec.iccDecoder)
            {
                // resize data - todo - make quicker
                data.erase(data.begin());
                data.erase(data.begin());
                dec.iccDecoder(dec, data);
            }
        }
        else if (DecodeApp(dec, mpHeader, input, data))
        {
            dec.logi(format("APP-2: Has Multi-Picture profile of length {}\n", data.size()));
            if (dec.mpfDecoder)
            {
                dec.mpfDecoder(dec, data);
            }
        }
        else if (DecodeApp(dec, fpxrHeader, input, data))
        {
            dec.logi(format("APP-2: Has FlashPix profile of length {}\n", data.size()));
            dec.logw("   - FlashPIX not supported\n");
        }
        else
        {
            dec.loge(format("Unsupported marker APP-2\n"));
            DumpPrefix(dec, input);
        }
        return true;
    }

    bool DecodeApp12(JpegDecoder& dec)
    { // https://exiftool.org/TagNames/APP12.html#PictureInfo
        vector<uint8_t> data, input;
        ReadSegment(dec, input);

        if (DecodeApp(dec, "Ducky", input, data))
        {
            dec.logi(format("APP-12: Has Ducky profile of length {}\n", data.size()));
            dec.logw(" -- Ducky decode not supported\n");
        }
        else
        {
            dec.loge(format("Unsupported marker APP-12 FFEC\n"));
            DumpPrefix(dec, input);
        }

        return true;
    }

    bool DecodeApp13(JpegDecoder& dec)
    {
        vector<uint8_t> data, input;
        ReadSegment(dec, input);

        if (DecodeApp(dec, "Photoshop 3.0", input, data)) {
            dec.logi(format("APP-13: Has Photoshop 3.0 profile of length {}\n", data.size()));
            dec.logw("  - format parse not implemented\n");
        }
        else
            dec.loge(format("Unsupported marker APP-13 FFED\n"));

        return true;
    }

    bool DecodeApp14(JpegDecoder& dec)
    {
        // CMYK T-REC-T.872-201206, https://afpcinc.org/wp-content/uploads/2016/08/Presentation-Object-Subsets-for-AFP-03.pdf

        vector<uint8_t> data, input;
        ReadSegment(dec, input);

        string adobeHeader = "Adobe"s; // use C++ string literal with 2 embedded nulls

        if (DecodeApp(dec, adobeHeader, input, data))
        {
            dec.logi(format("APP-14: Has Adobe info of length {}\n", data.size()));
            dec.logw("   - Adobe APP-14 not supported\n");
        }
        else
        {
            dec.loge(format("Unsupported marker APP-14\n"));
            DumpPrefix(dec, input);
        }
        return true;
    }

    bool DecodeCOM(JpegDecoder& dec)
    {
        int len = read2(dec);
        if (len >= 2) len -= 2;

        string s = "";
        while (len-- > 0)
        {
            char c = dec.read(); // NOTE: COM string may or may not have 0 terminator
            s += c;
        }
        dec.logi(format("COM: {}\n", s));
        return true;
    }

    bool DecodeDRI(JpegDecoder& dec)
    {
        int len = read2(dec);
        if (len >= 2) len -= 2;
        dec.decodeInterval = read2(dec);

        dec.logi(format("DRI: {}\n", dec.decodeInterval));
        return true;
    }

    bool skipNext(JpegDecoder& dec)
    {
        uint16_t len = read2(dec);
        if (len >= 2) len -= 2;
        while (len-- > 0)
            dec.read();
        return true;
    }

    bool Unsupported(JpegDecoder& dec)
    {
        dec.loge(format("unsupported marker {:02X}\n", dec.seg));
        vector<uint8_t> input;
        ReadSegment(dec, input);
        DumpPrefix(dec, input);


        //skipNext(dec);
        return true;
    }
    bool Succeed(JpegDecoder& dec)
    {
        return true;
    }
    bool Fail(JpegDecoder& dec)
    {
        return false;
    }

    struct Jump
    {
        uint16_t code;
        string txt;
        function<bool(JpegDecoder&)> func;
    };
    const static Jump jumps[] =
    {
        {0xFFC0,"SOF0",DecodeSOF},   // start of frame, baseline DCT
        {0xFFC1,"SOF1",Unsupported}, // start of frame 1, Extended sequential DCT
        {0xFFC2,"SOF2",Fail}, // start of frame 2, Progressive DCT
        {0xFFC3,"SOF3",Unsupported}, // start of frame 3, Lossless (Sequential)
        {0xFFC4,"DHT",DecodeDHT}, // Huffman tables, 4 for color, 2 for gray
        {0xFFC5,"SOF5",Unsupported}, // start of frame 5, Differential Sequential DCT
        {0xFFC6,"SOF6",Unsupported}, // start of frame 6, Differential Progressive DCT
        {0xFFC7,"SOF7",Unsupported}, // start of frame 7, Differential Lossless DCT
        {0xFFC8,"SOF8",Unsupported}, // jpeg extensions
        {0xFFC9,"SOF9",Unsupported}, // Extended Sequential DCT, Arithmetic coding
        {0xFFCA,"SOFA",Unsupported}, // Progressive DCT, Arithmetic coding
        {0xFFCB,"SOFB",Unsupported}, // Lossless DCT, Arithmetic coding
        {0xFFCC,"DAC",Fail}, // Define Arithmetic Coding
        {0xFFCD,"SOFD",Unsupported}, // Differential Sequential DCT, Arithmetic coding
        {0xFFCE,"SOFE",Unsupported}, // Differential Progressive DCT, Arithmetic coding
        {0xFFCF,"SOFF",Unsupported}, // Differential Lossless DCT, Arithmetic coding

        // restart markers should only be in the compressed sections, should not show up here
        {0xFFD0,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD1,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD2,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD3,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD4,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD5,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD6,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD7,"Restart",Unsupported}, // restart markers 0-7
        {0xFFD8,"SOI",Succeed}, // start of image
        {0xFFD9,"EOI",Fail}, // end of image
        {0xFFDA,"SOS",DecodeSOS}, // Start of scan
        {0xFFDB,"DQT",DecodeDQT}, // Decode quant table
        {0xFFDC,"DNL",Unsupported}, // Define number of lines
        {0xFFDD,"DRI",DecodeDRI}, // Define Restart Interval
        {0xFFDE,"DHP",Unsupported}, // Define Hierarchical Progression
        {0xFFDF,"EXP",Unsupported}, // expand reference component

        // 0xFFEx - app segments
        // see some standards at https://www.ozhiker.com/electronics/pjmt/jpeg_info/standards.html 
        {0xFFE0,"APP-0",DecodeApp0}, // required right after SOI, JFIF
        {0xFFE1,"APP-1",DecodeApp1}, // EXIF, TIFF, DCF, XMP
        {0xFFE2,"APP-2",DecodeApp2}, // ICC
        {0xFFE3,"APP-3",Unsupported}, // 
        {0xFFE4,"APP-4",Unsupported}, // 
        {0xFFE5,"APP-5",Unsupported}, // 
        {0xFFE6,"APP-6",Unsupported}, // 
        {0xFFE7,"APP-7",Unsupported}, // 
        {0xFFE8,"APP-8",Unsupported}, // 
        {0xFFE9,"APP-9",Unsupported}, // 
        {0xFFEA,"APP-10",Unsupported}, // 
        {0xFFEB,"APP-11",Unsupported}, // 
        {0xFFEC,"APP-12",DecodeApp12}, // Picture info as text
        {0xFFED,"APP-13",DecodeApp13}, // 
        {0xFFEE,"APP-14",DecodeApp14}, // 
        {0xFFEF,"APP-15",Unsupported}, // 

        // 0xFFFx 0-13: - extensions
        {0xFFF0,"JPG0",Unsupported}, // 
        {0xFFF1,"JPG1",Unsupported}, // 
        {0xFFF2,"JPG2",Unsupported}, // 
        {0xFFF3,"JPG3",Unsupported}, // 
        {0xFFF4,"JPG4",Unsupported}, // 
        {0xFFF5,"JPG5",Unsupported}, // 
        {0xFFF6,"JPG6",Unsupported}, // 
        {0xFFF7,"JPG7",Unsupported}, // 
        {0xFFF8,"JPG8",Unsupported}, // 
        {0xFFF9,"JPG9",Unsupported}, // 
        {0xFFFA,"JPG10",Unsupported}, // 
        {0xFFFB,"JPG11",Unsupported}, // 
        {0xFFFC,"JPG12",Unsupported}, // 
        {0xFFFD,"JPG13",Unsupported}, //
        {0xFFFE,"COM",DecodeCOM}, // comment
    };


    bool DecodeJpg(JpegDecoder& dec)
    {
        bool more = true;
        while (more && dec.lastCode == -1)
        {
            int offset = dec.offset;
            uint16_t seg = read2(dec);
            string txt = "???";

            if (0xFFC0 <= seg)
            {
                dec.seg = seg;
                const auto& j = jumps[seg - 0xFFC0];
                more = j.func(dec);
                txt = j.txt;
            }
            else
            {
                dec.loge(format("Unknown marker {:02X}, offset {} exiting.\n", seg, offset));
                skipNext(dec);
                more = false;

            }
            int len = dec.offset - offset;
            dec.logv(format("Offset {}, marker {}, {:02X}, len {}\n", offset, txt, seg, len));
        }
        return true;
    }

    void WritePPM(const std::string& filename, const JpegDecoder& dec)
    { // https://netpbm.sourceforge.net/doc/ppm.html

        const auto& img = dec.image;
        ofstream file(filename);
        file << "P3\n"; // color 24 bit, ASCII
        file << "# example image, Chris Lomont jpeg decoder\n";
        file << img->w << " " << img->h << "\n"; // width height
        file << 255 << endl; // max value
        auto p = img->data.data();
        auto rowsize = img->w * img->channels;
        for (int index = 0; index < rowsize * img->h; index += img->channels)
        {
            file << format("{} {} {} ",
                p[index],
                p[index + 1],
                p[index + 2]
            );
            if ((index % rowsize) == rowsize - img->channels)
                file << "\n";
        }
        file.close();
        cout << filename << " written\n";
    }


    void Decode(string filename, JpegDecoder& dec)
    {

        ifstream instream(filename, ios::in | ios::binary);

        vector<uint8_t> d((istreambuf_iterator<char>(instream)), istreambuf_iterator<char>());

        dec.d = d;
        dec.offset = 0;

        // attach some decoders
#define AddDecoder(dest,type) dec.dest = [](Logger& logger, const vector<uint8_t>& data){ type e; return e.Decode(logger,data);}

        AddDecoder(exifDecoder, ExifDecoder);
        AddDecoder(iccDecoder, IccDecoder);
        AddDecoder(xmpDecoder, XmpDecoder);
        AddDecoder(mpfDecoder, MpfDecoder);

#undef AddDecoder

        // set output
        if (!dec.output)
            dec.output = [](const string& msg) {cout << msg; };

        dec.logi(format(" filename {} has length {}\n", filename, d.size()));

        DecodeJpg(dec);
    }


}
// end of file
