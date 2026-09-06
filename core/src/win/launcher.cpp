#include "mm/win/launcher.hpp"

namespace mm::win {

namespace {

DWORD priority_class(Priority p) {
    switch (p) {
    case Priority::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
    case Priority::Idle:        return IDLE_PRIORITY_CLASS;
    case Priority::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
    case Priority::Normal:
    default:                    return NORMAL_PRIORITY_CLASS;
    }
}

std::wstring exe_directory(const std::wstring& exe) {
    const auto slash = exe.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash);
}

bool configure_job(HANDLE job, const LaunchOptions& opt, std::string& error) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ext{};
    ext.BasicLimitInformation.LimitFlags = opt.kill_on_job_close ? JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE : 0;
    if (opt.memory_limit_bytes != 0) {
        ext.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        ext.ProcessMemoryLimit = static_cast<SIZE_T>(opt.memory_limit_bytes);
    }
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ext, sizeof ext)) {
        error = "SetInformationJobObject(ExtendedLimit): " + last_error_message();
        return false;
    }
    if (opt.cpu_cap_percent != 0) {
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
        cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpu.CpuRate = static_cast<DWORD>((opt.cpu_cap_percent > 100 ? 100 : opt.cpu_cap_percent) * 100);
        if (!SetInformationJobObject(job, JobObjectCpuRateControlInformation, &cpu, sizeof cpu)) {
            error = "SetInformationJobObject(CpuRateControl): " + last_error_message();
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<Launched> launch(const LaunchOptions& opt, std::string& error) {
    std::wstring cmd = L"\"" + opt.exe + L"\"";
    if (!opt.args.empty()) cmd += L" " + opt.args;
    cmd.push_back(L'\0');  // CreateProcessW may modify the buffer in place

    const std::wstring cwd = opt.cwd.empty() ? exe_directory(opt.exe) : opt.cwd;

    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(opt.exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        // Our own job forbids breakaway (some terminals); try inside it.
        flags &= ~CREATE_BREAKAWAY_FROM_JOB;
        ok = CreateProcessW(opt.exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr,
                            cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    }
    if (!ok) {
        error = "CreateProcess: " + last_error_message();
        return std::nullopt;
    }

    Launched out;
    out.process.reset(pi.hProcess);
    UniqueHandle thread(pi.hThread);
    out.pid = pi.dwProcessId;

    out.job.reset(CreateJobObjectW(nullptr, nullptr));
    if (!out.job) {
        error = "CreateJobObject: " + last_error_message();
        TerminateProcess(pi.hProcess, 1);
        return std::nullopt;
    }
    if (!configure_job(out.job.get(), opt, error) || !AssignProcessToJobObject(out.job.get(), pi.hProcess)) {
        if (error.empty()) error = "AssignProcessToJobObject: " + last_error_message();
        TerminateProcess(pi.hProcess, 1);
        return std::nullopt;
    }

    std::string policy_error;
    if (!apply_policy(pi.hProcess, opt.policy, &policy_error)) {
        // Not fatal: the client still runs, just without the requested scheduling.
        error = policy_error;
    }

    ResumeThread(thread.get());
    return out;
}

bool apply_policy(HANDLE process, const ProcessPolicy& policy, std::string* error) {
    bool ok = true;
    std::string err;
    if (!SetPriorityClass(process, priority_class(policy.priority))) {
        ok = false;
        err += "SetPriorityClass: " + last_error_message() + "; ";
    }
    PROCESS_POWER_THROTTLING_STATE eco{};
    eco.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    eco.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    eco.StateMask = policy.eco_qos ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    if (!SetProcessInformation(process, ProcessPowerThrottling, &eco, sizeof eco)) {
        ok = false;
        err += "SetProcessInformation(PowerThrottling): " + last_error_message() + "; ";
    }
    if (policy.affinity_mask != 0 && !SetProcessAffinityMask(process, static_cast<DWORD_PTR>(policy.affinity_mask))) {
        ok = false;
        err += "SetProcessAffinityMask: " + last_error_message() + "; ";
    }
    if (!ok && error) *error = err;
    return ok;
}

UniqueHandle open_process_for_policy(DWORD pid) {
    return UniqueHandle(OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                    FALSE, pid));
}

bool has_exited(HANDLE process, DWORD* exit_code) {
    if (!process) return false;
    if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    if (exit_code) *exit_code = code;
    return true;
}

}  // namespace mm::win
