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
        auto res = co_await client.recv();
        if (!res) {
            fmt::println("Receive failed: {}", res.unwrapErr());
            break;
        }
        auto msg = std::move(res).unwrap();

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

    int specificCase = -1;
    if (argc > 2) {
        specificCase = std::atoi(argv[2]);
    }

    for (int i = 1; i <= 247; i++) {
        if (specificCase != -1 && i != specificCase) {
            continue;
        }

        std::println("Running case {}...", i);
        auto res = co_await runCase(url, i);

        if (!res) {
            std::println("Case {} failed: {}", i, res.unwrapErr());
        } else {
            std::println("Case {} passed", i);
        }
    }

    std::string updateUrl = fmt::format("{}/updateReports?agent=wsx", url);
    auto updateRes = (co_await wsx::connectAsync(updateUrl)).mapErr([](auto&& err) {
        return fmt::format("Connection failed: {}", err);
    });
    GEODE_CO_UNWRAP_INTO(auto updateClient, std::move(updateRes));
    GEODE_CO_UNWRAP(co_await updateClient.close());

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