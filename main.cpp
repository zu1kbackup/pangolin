#include "inject/pt_inject.h"
#include <common/cmdline.h>
#include <common/log.h>
#include <common/utils/shell.h>
#include <loader/payload.h>

constexpr auto PANGOLIN_WORKSPACE_SIZE = 0x10000;

constexpr auto SPREAD = "spread";
constexpr auto LOADER = "loader";
constexpr auto SHRINK = "shrink";

int main(int argc, char ** argv) {
    cmdline::parser parse;

    parse.add<int>("pid", 'p', "pid", true, 0);

    parse.add<std::string>("commandline", 'c', "inject commandline", true, "");
    parse.add<std::string>("env", 'e', "environment variable", false, "");

    parse.parse_check(argc, argv);

    INIT_CONSOLE_LOG(INFO);

    int pid = parse.get<int>("pid");

    std::string commandline = parse.get<std::string>("commandline");
    std::string env = parse.get<std::string>("env");

    LOG_INFO("inject '%s' to process %d", commandline.c_str(), pid);

    CPTInject ptInject(pid);

    if (!ptInject.init()) {
        LOG_ERROR("ptrace injector init failed");
        return -1;
    }

    if (!ptInject.attach()) {
        LOG_ERROR("ptrace injector attach failed");
        return -1;
    }

    void *result = nullptr;

    if (!ptInject.call(SPREAD, nullptr, (void *) PANGOLIN_WORKSPACE_SIZE, &result)) {
        LOG_ERROR("call spread shellcode failed");
        return -1;
    }

    LOG_INFO("workspace: %p", result);

    std::list<std::string> arguments;
    std::list<std::string> environs;

    if (!CShellAPI::expansion(commandline, arguments) || !CShellAPI::expansion(env, environs)) {
        LOG_ERROR("shell expansion failed");
        return -1;
    }

    std::string combinedArg = CStringHelper::join(arguments, PAYLOAD_DELIMITER);
    std::string combinedEnv = CStringHelper::join(environs, PAYLOAD_DELIMITER);

    if (combinedArg.size() >= sizeof(CPayload::argv) || combinedEnv.size() >= sizeof(CPayload::env)) {
        LOG_ERROR("payload size limit");
        return -1;
    }

    CPayload payload = {};

    memcpy(payload.argv, combinedArg.data(), combinedArg.size());
    memcpy(payload.env, combinedEnv.data(), combinedEnv.size());

    ptInject.writeMemory(result, &payload, sizeof(payload));

    int status = 0;
    unsigned long base = ((unsigned long)result + sizeof(payload) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (!ptInject.run(LOADER, (void *) base, result, status)) {
        LOG_ERROR("run loader shellcode failed");
        return -1;
    }

    LOG_INFO("free workspace: %p", result);

    if (!ptInject.call(SHRINK, nullptr, result, nullptr)) {
        LOG_ERROR("call shrink shellcode failed");
        return -1;
    }

    ptInject.detach();

    return status;
}
