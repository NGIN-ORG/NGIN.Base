#include <NGIN/Net/Runtime/NetworkDriver.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>
#include <NGIN/Net/Transport/TcpByteStream.hpp>
#include <NGIN/Net/Transport/UdpDatagramChannel.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Net public headers compile together")
{
    SUCCEED();
}
