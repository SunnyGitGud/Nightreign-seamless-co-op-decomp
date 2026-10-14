#pragma once
namespace nrsc::dll {
// Starts the init thread (_beginthreadex) and blocks until it has set the ready flag (or died on a fatal error).
void start_and_wait();
}  // namespace nrsc::dll
