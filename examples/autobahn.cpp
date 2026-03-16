#ifdef WSX_ENABLE_ASYNC

#include <wsx/AsyncClient.hpp>
#include <arc/prelude.hpp>
#include <print>
#include <iostream>

using namespace arc;

Future<Result<>> runCase(std::string url, int i) {
    auto full = fmt::format("{}/runCase?case={}&agent=wsx", url, i);

    auto res = (co_await wsx::connectAsync(full)).mapErr([](auto&& err) {
        return fmt::format("Connection failed: {}", err);
    });

    GEODE_CO_UNWRAP_INTO(auto client, std::move(res));

    while (true) {
        GEODE_CO_UNWRAP_INTO(auto msg, co_await client.recv());

        if (msg.isText()) {
            GEODE_CO_UNWRAP(co_await client.send(msg.text()));
        } else if (msg.isClose()) {
            break;
        } else {
            GEODE_CO_UNWRAP(co_await client.send(msg.data()));
        }
    }

    if (client.isConnected()) {
        GEODE_CO_UNWRAP(co_await client.close());
    }

    co_return Ok();
}

Future<Result<>> asyncMain(int argc, const char** argv) {
    std::string url = "ws://localhost:9001";
    if (argc > 1) {
        url = argv[1];
    }

    for (int i = 1; i <= 247; i++) {
        std::println("Running case {}...", i);
        auto res = co_await runCase(url, i);

        if (!res) {
            std::println("Case {} failed: {}", i, res.unwrapErr());
            break;
        } else {
            std::println("Case {} passed", i);
        }
    }

    co_return Ok();
}

ARC_DEFINE_MAIN(asyncMain);

#else

#include <print>

int main() {
    std::println("This example requires wsx to be built with async support.");
    return 1;
}

#endif