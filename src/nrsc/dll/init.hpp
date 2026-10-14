#pragma once
namespace nrsc::dll {
// Starts the init thread (_beginthreadex) and blocks until it has set the ready flag (or died on a fatal error).
void start_and_wait();
// Appends a line to nrsc_reconstruction.log next to the DLL (no-op before the init thread knows the DLL directory).
void log_line(const char* fmt, ...);
}  // namespace nrsc::dll
