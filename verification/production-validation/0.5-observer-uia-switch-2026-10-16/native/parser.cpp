#define wmain observer_application_main
#include OBSERVER_SOURCE
#undef wmain
#include <fstream>
#include <cstdio>

// Compile the production parser unchanged. Each case runs in a fresh process;
// this driver never calls the observer entry point or launches a build.
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        const auto file = full(argv[1]);
        handle input(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        check(input.valid(), "driver-open");
        const auto expected = sha256(input.get());
        std::ifstream stream(file);
        const auto metadata = nlohmann::json::parse(stream);
        const auto wanted = wide(metadata["nonce"].get<std::string>());
        load_input(file, expected, wanted, full(wide(metadata["report"].get<std::string>())));
        if (std::wcscmp(argv[2], L"accept") != 0) return 3;
        std::puts("native-input-accepted");
        return 0;
    } catch (const api_error& error) {
        std::printf("native-input-rejected|%s|%lu\n", error.what(), error.code);
        return wide(error.what()) == argv[2] ? 0 : 1;
    } catch (const std::exception& error) {
        std::printf("driver-error|%s\n", error.what());
        return 4;
    }
}
