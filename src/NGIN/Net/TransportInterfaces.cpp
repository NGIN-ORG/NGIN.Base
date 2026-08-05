#include <NGIN/Net/Transport/IByteStream.hpp>
#include <NGIN/Net/Transport/IDatagramChannel.hpp>

namespace NGIN::Net::Transport
{
    IByteStream::IByteStream() noexcept = default;
    IByteStream::~IByteStream()         = default;

    IDatagramChannel::IDatagramChannel() noexcept = default;
    IDatagramChannel::~IDatagramChannel()         = default;
}// namespace NGIN::Net::Transport
