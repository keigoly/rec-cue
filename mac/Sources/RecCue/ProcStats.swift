import Darwin
import Foundation
import IOKit.pwr_mgt

/// CPU and memory of this process and of the Mac, read every 10 s for the stats line.
enum Proc {
    static let ncpu = max(1, ProcessInfo.processInfo.activeProcessorCount)

    /// User + system CPU time of this process, in seconds.
    static func cpuSeconds() -> Double {
        var ru = rusage()
        getrusage(RUSAGE_SELF, &ru)
        return Double(ru.ru_utime.tv_sec) + Double(ru.ru_utime.tv_usec) / 1e6 + Double(ru.ru_stime.tv_sec)
            + Double(ru.ru_stime.tv_usec) / 1e6
    }

    /// phys_footprint (what Activity Monitor shows as Memory) and the resident size, in MB.
    static func memory() -> (footprint: Double, resident: Double) {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size)
        let kr = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return (0, 0) }
        return (Double(info.phys_footprint) / 1048576.0, Double(info.resident_size) / 1048576.0)
    }

    /// The power assertions this process holds (e.g. PreventUserIdleSystemSleep while recording).
    static func assertions() -> [String] {
        var byProcess: Unmanaged<CFDictionary>?
        guard IOPMCopyAssertionsByProcess(&byProcess) == kIOReturnSuccess,
              let all = byProcess?.takeRetainedValue() as? [NSNumber: [[String: Any]]] else { return [] }
        let mine = all.first { $0.key.int32Value == getpid() }?.value ?? []
        return mine.compactMap { $0[kIOPMAssertionTypeKey] as? String }.sorted()
    }

    /// Free memory of the Mac (free + speculative pages), in MB.
    static func systemFreeMB() -> Double {
        var vm = vm_statistics64_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        let host = mach_host_self()
        defer { mach_port_deallocate(mach_task_self_, host) }
        let kr = withUnsafeMutablePointer(to: &vm) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(host, HOST_VM_INFO64, $0, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return 0 }
        return Double(UInt64(vm.free_count) + UInt64(vm.speculative_count)) * Double(vm_kernel_page_size) / 1048576.0
    }
}
