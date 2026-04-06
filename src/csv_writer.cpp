#include "csv_writer.h"

#include <fstream>

namespace pr {

bool write_benchmark_csv(const std::string& path, const std::vector<std::string>& rows) {
  std::ofstream out(path);
  if (!out) return false;
  out << "scene,width,height,spp,depth,threads,schedule,tile_size,run,ms,pixels_per_sec\n";
  for (const auto& row : rows) out << row << '\n';
  return static_cast<bool>(out);
}

}  // namespace pr
