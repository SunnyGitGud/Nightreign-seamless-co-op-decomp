#pragma once

#include <stdexcept>
#include <string>

namespace nrsc {

// The original routes every unrecoverable condition through FUN_180029f40(message, source_file, line),
// which shows a "Seamless Coop %s - Fatal Error" message box and terminates. That routine is virtualized,
// so its exact termination behaviour is unknown. The reconstruction raises this exception instead and
// keeps the original source-file label and line number so call sites stay traceable.
//
// ev: FUN_180029f40 (virtualized stub), FUN_18002a020 (title formatter, ev_b73585be854191f1)
class FatalError : public std::runtime_error {
public:
    FatalError(std::string message, std::string source_file, unsigned line)
        : std::runtime_error(message),
          source_file_(std::move(source_file)),
          line_(line) {}

    const std::string& source_file() const noexcept { return source_file_; }
    unsigned line() const noexcept { return line_; }

private:
    std::string source_file_;
    unsigned line_;
};

}  // namespace nrsc
