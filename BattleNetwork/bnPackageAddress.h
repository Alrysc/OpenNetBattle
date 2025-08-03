#pragma once
#include "stx/result.h"
#include "stx/string.h"

struct PackageAddress {
  static const char nsDelim = '/';

  std::string namespaceId, packageId;

  PackageAddress();
  PackageAddress(const std::string& ns, const std::string& id);
  PackageAddress(const PackageAddress& rhs);
  operator std::string() const;
  const bool HasID() const { return !packageId.empty(); }

  static stx::result_t<PackageAddress> FromStr(const std::string& fqn);
};

namespace InternalPackages {
  static const PackageAddress sea_tile_boost = PackageAddress::FromStr("@internal/com.onb.sea_tile_boost").unwrap();

  namespace hashes {
    static const int32_t sea_tile_boost = stx::hash(InternalPackages::sea_tile_boost);
  }
}

bool operator<(const PackageAddress& a, const PackageAddress& b);
bool operator==(const PackageAddress& a, const PackageAddress& b);

struct PackageHash {
  std::string packageId;
  std::string md5;
};

bool operator<(const PackageHash& a, const PackageHash& b);
bool operator==(const PackageHash& a, const PackageHash& b);