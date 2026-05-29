#pragma once

#include <optional>
#include <string>

#include "archive_types.h"

namespace z7::app {

class BenchmarkTypedParser {
 public:
  std::optional<BenchmarkTypedSnapshot> consume_line(const std::string& line);
  const BenchmarkTypedSummary& summary() const;
  std::string summary_line() const;

 private:
  BenchmarkTypedSummary summary_;
};

}  // namespace z7::app
