#pragma once

/// @file Net.hpp
/// @brief Convenience surface for addressing, resolution, sockets, and plaintext transports.

#include <NGIN/Net/Resolve.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>
#include <NGIN/Net/Sockets/TcpListener.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>
#include <NGIN/Net/Transport/ByteStreamBuilder.hpp>
#include <NGIN/Net/Transport/DatagramBuilder.hpp>
#include <NGIN/Net/Transport/Filters/LengthPrefixedMessageStream.hpp>
#include <NGIN/Net/Transport/IByteStream.hpp>
#include <NGIN/Net/Transport/IDatagramChannel.hpp>
#include <NGIN/Net/Transport/TcpByteStream.hpp>
#include <NGIN/Net/Transport/UdpDatagramChannel.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/IpAddress.hpp>
