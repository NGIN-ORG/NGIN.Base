#include <NGIN/Net.hpp>
#include <NGIN/Serialization.hpp>

int main()
{
    const auto address  = NGIN::Net::IpAddress::Parse("127.0.0.1");
    const auto document = NGIN::Serialization::JSON::Parse(
            NGIN::Serialization::OwnedTextBuffer {R"({"installed":true})"});
    return address && document ? 0 : 1;
}
