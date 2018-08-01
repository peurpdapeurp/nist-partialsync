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
 *

 * This file incorporates work covered by the following copyright and
 * permission notice:

 * The MIT License (MIT)

 * Copyright (c) 2014 Gavin Andresen

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#ifndef PSYNC_IBLT_HPP
#define PSYNC_IBLT_HPP

#include <ndn-cxx/name.hpp>

#include <inttypes.h>
#include <set>
#include <vector>
#include <string>

namespace psync {

class HashTableEntry
{
public:
  int32_t count;
  uint32_t keySum;
  uint32_t keyCheck;

  bool isPure() const;
  bool empty() const;
};

/* Invertible Bloom Lookup Table
 * https://github.com/gavinandresen/IBLT_Cplusplus
 */
class IBLT
{
public:
  IBLT(size_t _expectedNumEntries);
  IBLT(const IBLT& other);
  IBLT(size_t _expectedNumEntries, std::vector <uint32_t> values);

  void insert(uint32_t key);
  void erase(uint32_t key);
  bool listEntries(std::set<uint32_t>& positive, std::set<uint32_t>& negative) const;
  IBLT operator-(const IBLT& other) const;
  bool operator==(const IBLT& other) const;

  std::vector<HashTableEntry>
  getHashTable() const
  {
    return hashTable;
  }

  std::size_t
  getNumEntry() {
    return hashTable.size();
  }

  void
  appendToName(ndn::Name& name) const;

  IBLT
  getIBLTFromName(size_t expectedNumEntries, size_t ibltSize,
                  const ndn::name::Component& ibltName) const;

public:
  // for debugging
  std::string DumpTable() const;

private:
  void _insert(int plusOrMinus, uint32_t key);

private:
  std::vector<HashTableEntry> hashTable;
};

} // namespace psync

#endif // PSYNC_IBLT_HPP