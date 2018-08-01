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

#include "util.hpp"
#include <string>

namespace psync {

uint32_t
ROTL32 ( uint32_t x, int8_t r )
{
  return (x << r) | (x >> (32 - r));
}

uint32_t
MurmurHash3(uint32_t nHashSeed, const std::vector<unsigned char>& vDataToHash)
{
  // The following is MurmurHash3 (x86_32),
  // see http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp
  uint32_t h1 = nHashSeed;
  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  const size_t nblocks = vDataToHash.size() / 4;

  //----------
  // body
  const uint32_t * blocks = (const uint32_t *)(&vDataToHash[0] + nblocks*4);

  for (size_t i = -nblocks; i; i++) {
    uint32_t k1 = blocks[i];

    k1 *= c1;
    k1 = ROTL32(k1,15);
    k1 *= c2;

    h1 ^= k1;
    h1 = ROTL32(h1,13);
    h1 = h1*5+0xe6546b64;
  }

  //----------
  // tail
  const uint8_t * tail = (const uint8_t*)(&vDataToHash[0] + nblocks*4);

  uint32_t k1 = 0;

  // gcc gives "warning: this statement may fall through"
  // Need either fall through or break here
  switch (vDataToHash.size() & 3) {
    case 3: k1 ^= tail[2] << 16;
    [[fallthrough]];
    case 2: k1 ^= tail[1] << 8;
    [[fallthrough]];
    case 1: k1 ^= tail[0];
    k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization
  h1 ^= vDataToHash.size();
  h1 ^= h1 >> 16;
  h1 *= 0x85ebca6b;
  h1 ^= h1 >> 13;
  h1 *= 0xc2b2ae35;
  h1 ^= h1 >> 16;

  return h1;
}

std::vector<unsigned char>
ParseHex(const std::string& str)
{
  int len = str.length();
  std::vector <unsigned char> vch;

  for (int i = 0; i < len; i++) {
    vch.push_back((unsigned char)str[i]);
  }

  return vch;
}

size_t
getSize(uint64_t varNumber)
{
  std::size_t ans = 1;
  if (varNumber < 253) {
    ans += 1;
  }
  else if (varNumber <= std::numeric_limits<uint16_t>::max()) {
    ans += 3;
  }
  else if (varNumber <= std::numeric_limits<uint32_t>::max()) {
    ans += 5;
  }
  else {
    ans += 9;
  }

  return ans;
}

} // namespace psync
