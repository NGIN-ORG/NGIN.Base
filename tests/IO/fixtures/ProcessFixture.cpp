#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    [[nodiscard]] int ParseInteger(const char* value)
    {
        try
        {
            return std::stoi(value);
        } catch (...)
        {
            return -1;
        }
    }
}// namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        return 64;
    }

    const std::string mode {argv[1]};
    if (mode == "arguments")
    {
        for (int index = 2; index < argc; ++index)
        {
            if (index != 2)
            {
                std::cout << '|';
            }
            std::cout << argv[index];
        }
        return 0;
    }
    if (mode == "streams")
    {
        std::cout << "standard-output";
        std::cerr << "standard-error";
        return 0;
    }
    if (mode == "environment")
    {
        if (argc < 3)
        {
            return 64;
        }
        if (const char* value = std::getenv(argv[2]))
        {
            std::cout << value;
            return 0;
        }
        return 3;
    }
    if (mode == "working-directory")
    {
        std::cout << std::filesystem::current_path().generic_string();
        return 0;
    }
    if (mode == "sleep")
    {
        if (argc < 3)
        {
            return 64;
        }
        const int milliseconds = ParseInteger(argv[2]);
        if (milliseconds < 0)
        {
            return 65;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds {milliseconds});
        return 0;
    }
    if (mode == "exit")
    {
        return argc >= 3 ? ParseInteger(argv[2]) : 64;
    }
    if (mode == "spam")
    {
        if (argc < 3)
        {
            return 64;
        }
        const int count = ParseInteger(argv[2]);
        if (count < 0)
        {
            return 65;
        }
        for (int index = 0; index < count; ++index)
        {
            std::cout << 'x';
        }
        return 0;
    }
    if (mode == "delayed-file")
    {
        if (argc < 3)
        {
            return 64;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds {750});
        std::ofstream {argv[2]} << "created";
        return 0;
    }
    if (mode == "process-tree")
    {
        if (argc < 3)
        {
            return 64;
        }
#if defined(_WIN32)
        std::string command = "\"" + std::string {argv[0]} + "\" delayed-file \"" + argv[2] + "\"";
        command.push_back('\0');
        STARTUPINFOA startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process {};
        if (!::CreateProcessA(
                    nullptr,
                    command.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    nullptr,
                    &startup,
                    &process))
        {
            return 70;
        }
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
#else
        const auto child = ::fork();
        if (child < 0)
        {
            return 70;
        }
        if (child == 0)
        {
            ::execl(argv[0], argv[0], "delayed-file", argv[2], nullptr);
            std::_Exit(71);
        }
#endif
        std::this_thread::sleep_for(std::chrono::seconds {5});
        return 0;
    }
    return 66;
}
