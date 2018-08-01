/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2018,  The University of Memphis
 *
 * This file is part of NLSR (Named-data Link State Routing).
 * See AUTHORS.md for complete list of NLSR authors and contributors.
 *
 * NLSR is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NLSR is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NLSR, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#ifndef PSYNC_UTIL_HPP
#define PSYNC_UTIL_HPP

#include "iblt.hpp"
#include <ndn-cxx/name.hpp>

#include <inttypes.h>
#include <vector>
#include <string>

namespace psync {

uint32_t
MurmurHash3(uint32_t nHashSeed, const std::vector<unsigned char>& vDataToHash);

std::vector<unsigned char>
ParseHex(const std::string& str);

struct MissingDataInfo
{
  MissingDataInfo(std::string prefix, uint32_t seq1, uint32_t seq2)
  :prefix(prefix)
  ,seq1(seq1)
  ,seq2(seq2)
  {
  }

  std::string prefix;
  uint32_t seq1;
  uint32_t seq2;
};

size_t
getSize(uint64_t varNumber);

} // namespace psync

#endif // PSYNC_UTIL_HPP
