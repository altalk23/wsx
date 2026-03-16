#ifdef WSX_ENABLE_ASYNC

#include <wsx/AsyncClient.hpp>
#include <arc/prelude.hpp>
#include <print>
#include <iostream>

using namespace arc;

Future<Result<>> asyncMain(int argc, const char** argv) {
    std::string url = "ws://localhost:8080";
    if (argc > 1) {
        url = argv[1];
    }

    GEODE_CO_UNWRAP_INTO(auto client, co_await wsx::connectAsync(url));

    while (true) {
        auto line = co_await arc::spawnBlocking<std::string>([] -> std::string {
            std::cout << "> " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) {
                return "";
            }
            return line;
        });

        if (line.empty()) {
            break;
        }

        GEODE_CO_UNWRAP(co_await client.send(line));
        GEODE_CO_UNWRAP_INTO(auto msg, co_await client.recv());

        if (msg.isText()) {
            std::println("{}", msg.text());
        } else {
            std::println("Received binary message of {} bytes", msg.data().size());
        }
    }

    std::println("Closing..");
    GEODE_CO_UNWRAP(co_await client.close());

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