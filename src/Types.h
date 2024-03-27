#pragma once
#include <string>
#include <functional>


namespace Lomont::Jpeg
{
    using namespace std;


    enum LogType
    {
        VERBOSE,
        INFO,
        WARN,
        ERROR
    };
    struct Logger
    {
        LogType logLevel = LogType::VERBOSE;

        void log(int type, const string& msg)
        {
            if (type >= logLevel && output)
                output(msg);
        }
        void logv(const string& msg) { ++verboseCount; log(LogType::VERBOSE, msg); }
        void logi(const string& msg) { ++infoCount; log(LogType::INFO, msg); }
        void logw(const string& msg) { ++warningCount; log(LogType::WARN, "WARNING: " + msg); }
        void loge(const string& msg) { ++errorCount; log(LogType::ERROR, "ERROR: " + msg); }
        function<void(const string& msg)> output{ nullptr };

        int verboseCount{ 0 }, infoCount{ 0 }, warningCount{ 0 }, errorCount{ 0 };
    };


    // base class for a marker decoder
    class Decoder
    {
    public:
        virtual bool Decode(Logger& dec, const vector<uint8_t>& data) = 0;
        virtual ~Decoder() {}

    protected:
        Logger* dec {nullptr};
        const vector<uint8_t> * data;
        int readPos{ 0 };

        int read(int n)
        {
            int v = 0;
            int len = n;
            for (int p = 0; p < n; ++p)
            {
                int d = (*data)[readPos++];
                if (isMoto)
                    v = 256 * v + d;
                else if (isIntel)
                {
                    d <<= p * 8;
                    v += d;
                }
            }
            return v;
        }


        // endian
        bool isIntel{ false }, isMoto{ false };

    };


}
