#pragma once
// Client process lifecycle: CreateProcess (suspended) -> Job Object ->
// scheduling policy -> resume. The same policy can be applied to adopted
// processes that we did not start.
#include <cstdint>
#include <optional>
#include <string>

#include "mm/win/win_common.hpp"

namespace mm::win {

enum class Priority : uint8_t { Normal, BelowNormal, Idle, AboveNormal };

struct ProcessPolicy {
    Priority priority = Priority::Normal;
    bool     eco_qos = false;        // Windows 11 "Efficiency mode" (EcoQoS)
    uint64_t affinity_mask = 0;      // 0 = leave unchanged
};

struct LaunchOptions {
    std::wstring exe;
    std::wstring args;               // appended after the quoted exe path
    std::wstring cwd;                // empty = exe's directory
    ProcessPolicy policy;
    unsigned cpu_cap_percent = 0;    // hard cap as % of TOTAL system CPU (Job CPU rate control); 0 = none
    uint64_t memory_limit_bytes = 0; // Job per-process commit limit; 0 = none
    bool     kill_on_job_close = false; // terminate the client when the controller exits (off: permadeath game)
};

struct Launched {
    UniqueHandle process;
    UniqueHandle job;
    DWORD pid = 0;
};

std::optional<Launched> launch(const LaunchOptions& opt, std::string& error);

// Applies priority / EcoQoS / affinity to a running process.
bool apply_policy(HANDLE process, const ProcessPolicy& policy, std::string* error);

// Opens a process with the rights needed by apply_policy and wait_for_exit.
UniqueHandle open_process_for_policy(DWORD pid);

bool has_exited(HANDLE process, DWORD* exit_code);

}  // namespace mm::win
