// Benchmark CSV output boundary.
// Depends only on standard library containers and strings.

#pragma once

#include <string>
#include <vector>

namespace pr {

bool write_benchmark_csv(const std::string& path, const std::vector<std::string>& rows);

}  // namespace pr
