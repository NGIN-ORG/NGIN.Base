#include <catch2/catch_test_macros.hpp>

#include "../../src/NGIN/Net/SocketPlatform.hpp"
#include "../../src/NGIN/Net/SocketState.hpp"

#include <barrier>
#include <thread>

namespace
{
    using namespace NGIN::Net;
    SocketHandle OpenSocket()
    {
        NetError error;
        auto     socket = detail::CreateSocket(AddressFamily::V4, SOCK_DGRAM, IPPROTO_UDP, true, error);
        if (!error.IsOk())
            throw std::system_error(ToErrorCode(error));
        return socket;
    }
    bool IsNativeAlive(SocketHandle::NativeHandle native)
    {
        int       type = 0;
        socklen_t size = sizeof(type);
        return ::getsockopt(static_cast<detail::NativeSocket>(native), SOL_SOCKET, SO_TYPE,
#if defined(NGIN_PLATFORM_WINDOWS)
                            reinterpret_cast<char*>(&type),
#else
                            &type,
#endif
                            &size) == 0;
    }
}// namespace

TEST_CASE("Socket leases admit one read and one write and defer native close until both retire", "[Net][Lifetime]")
{
    using namespace NGIN::Net;
    auto       socket = OpenSocket();
    auto       state  = detail::SocketHandleAccess::State(socket);
    const auto native = socket.Native();
    auto       read   = detail::SocketLease::Acquire(state, detail::SocketState::Read);
    auto       write  = detail::SocketLease::Acquire(state, detail::SocketState::Write);
    REQUIRE(read);
    REQUIRE(write);
    auto anotherRead  = detail::SocketLease::Acquire(state, detail::SocketState::Read);
    auto anotherWrite = detail::SocketLease::Acquire(state, detail::SocketState::Write);
    auto connect      = detail::SocketLease::Acquire(state, detail::SocketState::Exclusive);
    REQUIRE_FALSE(anotherRead);
    REQUIRE_FALSE(anotherWrite);
    REQUIRE_FALSE(connect);
    REQUIRE(anotherRead.error().code == NetErrorCode::OperationInProgress);
    REQUIRE(anotherWrite.error().code == NetErrorCode::OperationInProgress);
    REQUIRE(connect.error().code == NetErrorCode::OperationInProgress);
    socket.Close();
    REQUIRE_FALSE(socket.IsOpen());
    REQUIRE(read->CloseToken().IsCancellationRequested());
    REQUIRE(IsNativeAlive(native));
    auto rejected = detail::SocketLease::Acquire(state, detail::SocketState::Read);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == NetErrorCode::Disconnected);
    read->Reset();
    REQUIRE(IsNativeAlive(native));
    write->Reset();
    REQUIRE_FALSE(IsNativeAlive(native));
}

TEST_CASE("Socket leases recover direction capacity and preserve state through owner moves", "[Net][Lifetime]")
{
    using namespace NGIN::Net;
    auto original = OpenSocket();
    auto state    = detail::SocketHandleAccess::State(original);
    auto lease    = detail::SocketLease::Acquire(state, detail::SocketState::Read);
    REQUIRE(lease);
    const auto native = lease->Native();
    lease->Reset();
    auto exclusive = detail::SocketLease::Acquire(state, detail::SocketState::Exclusive);
    REQUIRE(exclusive);
    SocketHandle moved(std::move(original));
    REQUIRE_FALSE(original.IsOpen());
    original.Close();
    REQUIRE_FALSE(exclusive->IsClosing());
    REQUIRE(moved.Native() == native);
    moved.Close();
    REQUIRE(IsNativeAlive(native));
    auto retained = std::move(*exclusive);
    exclusive->Reset();
    REQUIRE(IsNativeAlive(native));
    retained.Reset();
    REQUIRE_FALSE(IsNativeAlive(native));
}

TEST_CASE("Socket close racing admission cannot release a successfully pinned native handle", "[Net][Lifetime][Race]")
{
    using namespace NGIN::Net;
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        auto         socket = OpenSocket();
        auto         state  = detail::SocketHandleAccess::State(socket);
        const auto   native = socket.Native();
        std::barrier start(3);
        bool         valid = true;
        std::thread  submit([&] {
            start.arrive_and_wait();
            auto lease = detail::SocketLease::Acquire(state, detail::SocketState::Read);
            if (lease)
                valid = IsNativeAlive(lease->Native());
            else
                valid = lease.error().code == NetErrorCode::Disconnected;
        });
        std::thread  closer([&] {
            start.arrive_and_wait();
            socket.Close();
        });
        start.arrive_and_wait();
        submit.join();
        closer.join();
        REQUIRE(valid);
        REQUIRE_FALSE(IsNativeAlive(native));
    }
}

TEST_CASE("Socket lease rejects absent state and invalid directions without native access", "[Net][Admission]")
{
    using namespace NGIN::Net;
    REQUIRE_FALSE(detail::SocketLease::Acquire({}, detail::SocketState::Read));
    auto socket  = OpenSocket();
    auto state   = detail::SocketHandleAccess::State(socket);
    auto empty   = detail::SocketLease::Acquire(state, 0);
    auto invalid = detail::SocketLease::Acquire(state, 4);
    REQUIRE_FALSE(empty);
    REQUIRE_FALSE(invalid);
    REQUIRE(empty.error().code == NetErrorCode::InvalidArgument);
    REQUIRE(invalid.error().code == NetErrorCode::InvalidArgument);
    REQUIRE(socket.IsOpen());
}
