#pragma once

#include <vector>

#include <pex/PexReader.h>
#include <pex/PexString.h>
#include <pex/PexWriter.h>

namespace caprica { namespace pex {

struct PexDebugStructOrder final
{
  PexString objectName{ };
  PexString structName{ };
  std::vector<PexString> members{ };

  explicit PexDebugStructOrder() = default;
  PexDebugStructOrder(const PexDebugStructOrder&) = delete;
  ~PexDebugStructOrder() = default;

  static PexDebugStructOrder* read(PexReader& rdr);
  void write(PexWriter& wtr) const;
};

}}
