#include <NGIN/Benchmark.hpp>
#include <NGIN/Text/String.hpp>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using NGIN::Benchmark;
using NGIN::BenchmarkContext;
using NGIN::Text::String;
using NGIN::Units::Milliseconds;

// Helper to generate random strings
template<typename StrType>
void fill_random_strings(std::vector<StrType>& vec, size_t count, size_t len)
{
    static const char                            charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::mt19937                          rng(123);
    static std::uniform_int_distribution<size_t> char_dist(0, sizeof(charset) - 2);
    vec.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        std::string s;
        s.reserve(len);
        for (size_t j = 0; j < len; ++j)
            s += charset[char_dist(rng)];
        vec[i] = StrType(s.c_str());
    }
}

template<typename StrType>
std::vector<StrType> make_string_vector(std::initializer_list<std::string> values)
{
    std::vector<StrType> result;
    result.reserve(values.size());
    for (const auto& value: values)
        result.emplace_back(value.c_str());
    return result;
}

int main()
{
    constexpr size_t N        = 10000;
    constexpr size_t shortLen = 8;
    constexpr size_t longLen  = 128;

    std::cout << "Object sizes: std::string=" << sizeof(std::string)
              << ", NGIN::Text::String=" << sizeof(String)
              << ", NGIN::Text::AnsiString=" << sizeof(NGIN::Text::AnsiString)
              << ", NGIN::Text::WString=" << sizeof(NGIN::Text::WString)
              << ", NGIN::Text::UTF16String=" << sizeof(NGIN::Text::UTF16String) << '\n';

    // --- Construction Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s("shortstr");
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s("shortstr");
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s(std::string(longLen, 'x'));
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string long construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> data;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s(std::string(longLen, 'x').c_str());
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String long construction");

    // --- Copy Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> src(N, "shortstr");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string short copy");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> src(N, String("shortstr"));
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String short copy");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> src(N, std::string(longLen, 'x'));
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            std::string s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "std::string long copy");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> src(N, String(std::string(longLen, 'x').c_str()));
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            String s = src[i];
            ctx.doNotOptimize(s);
        }
        ctx.stop();
    },
                        "NGIN::String long copy");

    // --- Append Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += "abc";
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string append short");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += "abc";
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String append short");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        std::string longstr(longLen, 'y');
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += longstr;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string append long");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        String longstr(std::string(longLen, 'y').c_str());
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s += longstr;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String append long");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s("ab");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.Append(3, 'x');
            s.Resize(2);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String append repeated char");

    // --- Random String Construction ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> vec;
        ctx.start();
        fill_random_strings(vec, N, shortLen);
        ctx.doNotOptimize(vec);
        ctx.stop();
    },
                        "std::string random short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> vec;
        ctx.start();
        fill_random_strings(vec, N, shortLen);
        ctx.doNotOptimize(vec);
        ctx.stop();
    },
                        "NGIN::String random short construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> vec;
        ctx.start();
        ctx.doNotOptimize(vec);
        fill_random_strings(vec, N, longLen);
        ctx.clobberMemory();
        ctx.stop();
    },
                        "std::string random long construction");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> vec;
        ctx.start();
        ctx.doNotOptimize(vec);
        fill_random_strings(vec, N, longLen);
        ctx.clobberMemory();
        ctx.stop();
    },
                        "NGIN::String random long construction");

    // --- CStr Access ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> vec(N, "shortstr");
        size_t                   sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += vec[i].c_str()[0];
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string c_str() access");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> vec(N, String("shortstr"));
        size_t              sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += vec[i].CStr()[0];
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String CStr() access");

    // --- Reserve/Capacity ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.reserve(i % 256);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string reserve");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.Reserve(i % 256);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String Reserve");

    // --- Assignment ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s;
        std::string t("shortstr");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = t;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string assignment");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s;
        String t("shortstr");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = t;
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String assignment");

    // --- Move Assignment ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<std::string> src(N, "shortstr");
        std::string              s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = std::move(src[i]);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string move assignment");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::vector<String> src(N, String("shortstr"));
        String              s;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s = std::move(src[i]);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String move assignment");

    // --- Mutation Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s("alphagamma");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.insert(5, "-beta");
            s.erase(5, 5);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string insert/erase middle");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s("alphagamma");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.Insert(5, "-beta");
            s.Erase(5, 5);
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String insert/erase middle");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s("alpha-x");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.replace(5, 2, "-beta-gamma");
            s.replace(5, 11, "-x");
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string replace grow/shrink");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s("alpha-x");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.Replace(5, 2, "-beta-gamma");
            s.Replace(5, 11, "-x");
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String replace grow/shrink");

    Benchmark::Register([](BenchmarkContext& ctx) {
        std::string s("prefix-payload-suffix");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.erase(0, 7);
            s.erase(s.size() - 7, 7);
            s.insert(0, "prefix-");
            s += "-suffix";
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "std::string prefix/suffix trim");

    Benchmark::Register([](BenchmarkContext& ctx) {
        String s("prefix-payload-suffix");
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            s.RemovePrefix(7);
            s.RemoveSuffix(7);
            s.Insert(0, "prefix-");
            s.Append("-suffix");
        }
        ctx.doNotOptimize(s);
        ctx.stop();
    },
                        "NGIN::String prefix/suffix trim");

    // --- Search Benchmarks ---
    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {"bananaban", "nanabanax", "abananabn", "xxnxxnxxx"};
        size_t                         sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find('n');
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find char SBO");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks =
                make_string_vector<String>({"bananaban", "nanabanax", "abananabn", "xxnxxnxxx"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].Find('n');
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String Find char SBO");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {"bananaban", "ananxxxx", "xxanxxan", "zzanazzz"};
        size_t                         sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find("an");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find short needle SBO");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks =
                make_string_vector<String>({"bananaban", "ananxxxx", "xxanxxan", "zzanazzz"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].Find("an");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String Find short needle SBO");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {
                std::string(80, 'a') + "needle" + std::string(80, 'a'),
                std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                std::string(90, 'a') + "needle" + std::string(10, 'a'),
                std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle",
        };
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find("needle");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find needle heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks = make_string_vector<String>(
                {std::string(80, 'a') + "needle" + std::string(80, 'a'),
                 std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                 std::string(90, 'a') + "needle" + std::string(10, 'a'),
                 std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].Find("needle");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String Find needle heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {
                std::string(80, 'a') + "needle" + std::string(80, 'a'),
                std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                std::string(90, 'a') + "needle" + std::string(10, 'a'),
                std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle",
        };
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].rfind("needle");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string rfind needle heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks = make_string_vector<String>(
                {std::string(80, 'a') + "needle" + std::string(80, 'a'),
                 std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                 std::string(90, 'a') + "needle" + std::string(10, 'a'),
                 std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].RFind("needle");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String RFind needle heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {
                std::string(80, 'a') + "needle" + std::string(80, 'a'),
                std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                std::string(90, 'a') + "needle" + std::string(10, 'a'),
                std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle",
        };
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find("missing");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find missing heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks = make_string_vector<String>(
                {std::string(80, 'a') + "needle" + std::string(80, 'a'),
                 std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                 std::string(90, 'a') + "needle" + std::string(10, 'a'),
                 std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].Find("missing");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String Find missing heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {
                std::string(80, 'a') + "needle" + std::string(80, 'a'),
                std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                std::string(90, 'a') + "needle" + std::string(10, 'a'),
                std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle",
        };
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find_first_of("xyzl");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find_first_of heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks = make_string_vector<String>(
                {std::string(80, 'a') + "needle" + std::string(80, 'a'),
                 std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                 std::string(90, 'a') + "needle" + std::string(10, 'a'),
                 std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].FindFirstOf("xyzl");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String FindFirstOf heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {
                std::string(80, 'a') + "needle" + std::string(80, 'a'),
                std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                std::string(90, 'a') + "needle" + std::string(10, 'a'),
                std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle",
        };
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find_last_of("xyzl");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find_last_of heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks = make_string_vector<String>(
                {std::string(80, 'a') + "needle" + std::string(80, 'a'),
                 std::string(20, 'a') + "needle" + std::string(80, 'a') + "needle",
                 std::string(90, 'a') + "needle" + std::string(10, 'a'),
                 std::string(30, 'a') + "needlz" + std::string(50, 'a') + "needle"});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].FindLastOf("xyzl");
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String FindLastOf heap");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {std::string(32, ' ') + "payload" + std::string(32, ' '),
                                                  std::string(8, ' ') + "payload" + std::string(24, ' '),
                                                  std::string(16, ' ') + "payload" + std::string(8, ' '),
                                                  std::string(4, ' ') + "payload" + std::string(40, ' ')};
        size_t                         sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find_first_not_of(' ');
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find_first_not_of");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks =
                make_string_vector<String>({std::string(32, ' ') + "payload" + std::string(32, ' '),
                                            std::string(8, ' ') + "payload" + std::string(24, ' '),
                                            std::string(16, ' ') + "payload" + std::string(8, ' '),
                                            std::string(4, ' ') + "payload" + std::string(40, ' ')});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].FindFirstNotOf(' ');
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String FindFirstNotOf");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const std::vector<std::string> haystacks {std::string(32, ' ') + "payload" + std::string(32, ' '),
                                                  std::string(8, ' ') + "payload" + std::string(24, ' '),
                                                  std::string(16, ' ') + "payload" + std::string(8, ' '),
                                                  std::string(4, ' ') + "payload" + std::string(40, ' ')};
        size_t                         sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].find_last_not_of(' ');
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "std::string find_last_not_of");

    Benchmark::Register([](BenchmarkContext& ctx) {
        const auto haystacks =
                make_string_vector<String>({std::string(32, ' ') + "payload" + std::string(32, ' '),
                                            std::string(8, ' ') + "payload" + std::string(24, ' '),
                                            std::string(16, ' ') + "payload" + std::string(8, ' '),
                                            std::string(4, ' ') + "payload" + std::string(40, ' ')});
        size_t sum = 0;
        ctx.start();
        for (size_t i = 0; i < N; ++i)
        {
            sum += haystacks[i % haystacks.size()].FindLastNotOf(' ');
        }
        ctx.doNotOptimize(sum);
        ctx.stop();
    },
                        "NGIN::String FindLastNotOf");

    // --- End: Run all and print ---
    Benchmark::defaultConfig.iterations       = 100;
    Benchmark::defaultConfig.warmupIterations = 5;
    auto results                              = Benchmark::RunAll<Milliseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
