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

#include "iblt.hpp"
#include "util.hpp"

#include <cassert>
#include <sstream>
#include <iostream>

namespace psync {

static const size_t N_HASH = 3;
static const size_t N_HASHCHECK = 11;

template<typename T>
std::vector<unsigned char> ToVec(T number)
{
  std::vector<unsigned char> v(sizeof(T));

  for (size_t i = 0; i < sizeof(T); i++) {
    v.at(i) = (number >> i*8) & 0xff;
  }

  return v;
}

bool
HashTableEntry::isPure() const
{
  if (count == 1 || count == -1) {
    uint32_t check = MurmurHash3(N_HASHCHECK, ToVec(keySum));
    return (keyCheck == check);
  }

  return false;
}

bool
HashTableEntry::empty() const
{
  return (count == 0 && keySum == 0 && keyCheck == 0);
}

IBLT::IBLT(size_t _expectedNumEntries)
{
  // 1.5x expectedNumEntries gives very low probability of
  // decoding failure
  size_t nEntries = _expectedNumEntries + _expectedNumEntries/2;
  // ... make nEntries exactly divisible by N_HASH
  size_t remainder = nEntries % N_HASH;
  if (remainder != 0) {
    nEntries += (N_HASH - remainder);
  }

  hashTable.resize(nEntries);
}

IBLT::IBLT(const IBLT& other)
{
  hashTable = other.hashTable;
}

IBLT::IBLT(size_t _expectedNumEntries, std::vector <uint32_t> values)
{
  size_t nEntries = _expectedNumEntries + _expectedNumEntries/2;

  size_t remainder = nEntries % N_HASH;
  if (remainder != 0) {
    nEntries += (N_HASH - remainder);
  }

  hashTable.resize(nEntries);

  assert(3 * hashTable.size() == values.size());

  for (size_t i = 0; i < hashTable.size(); i++) {
    HashTableEntry& entry = hashTable.at(i);
    if (values[i*3] != 0)
    entry.count = values[i*3];
    entry.keySum = values[i*3+1];
    entry.keyCheck = values[i*3+2];
  }
}

void
IBLT::_insert(int plusOrMinus, uint32_t key)
{
  std::vector<uint8_t> kvec = ToVec(key);

  size_t bucketsPerHash = hashTable.size()/N_HASH;
  for (size_t i = 0; i < N_HASH; i++) {
    size_t startEntry = i * bucketsPerHash;
    uint32_t h = MurmurHash3(i, kvec);
    HashTableEntry& entry = hashTable.at(startEntry + (h % bucketsPerHash));
    entry.count += plusOrMinus;
    entry.keySum ^= key;
    entry.keyCheck ^= MurmurHash3(N_HASHCHECK, kvec);
    //std::cout << "Index is: " << startEntry + (h%bucketsPerHash) << " " <<
    //  key << " " << MurmurHash3(N_HASHCHECK, kvec) << std::endl;
  }
}

void
IBLT::insert(uint32_t key)
{
  _insert(1, key);
}

void
IBLT::erase(uint32_t key)
{
  _insert(-1, key);
}

bool
IBLT::listEntries(std::set<uint32_t>& positive, std::set<uint32_t>& negative) const
{
  IBLT peeled = *this;

  // WHY DO WE NEED THE DO WHILE HERE?
  size_t nErased = 0;
  do {
    nErased = 0;
    for (size_t i = 0; i < peeled.hashTable.size(); i++) {
      HashTableEntry& entry = peeled.hashTable.at(i);
      if (entry.isPure()) {
        if (entry.count == 1) {
          positive.insert(entry.keySum);
        }
        else {
          negative.insert(entry.keySum);
        }
        peeled._insert(-entry.count, entry.keySum);
        ++nErased;
      }
    }
  } while (nErased > 0);

  // If any buckets for one of the hash functions is not empty,
  // then we didn't peel them all:
  for (size_t i = 0; i < peeled.hashTable.size(); i++) {
    if (peeled.hashTable.at(i).empty() != true) {
      return false;
    }
  }

  return true;
}

IBLT
IBLT::operator-(const IBLT& other) const
{
  //std::cout << hashTable.size() << " == " << other.hashTable.size() << std::endl;
  assert(hashTable.size() == other.hashTable.size());

  IBLT result(*this);
  for (size_t i = 0; i < hashTable.size(); i++) {
    HashTableEntry& e1 = result.hashTable.at(i);
    const HashTableEntry& e2 = other.hashTable.at(i);
    e1.count -= e2.count;
    e1.keySum ^= e2.keySum;
    e1.keyCheck ^= e2.keyCheck;
  }

  return result;
}

bool
IBLT::operator==(const IBLT& other) const
{
  if (this->hashTable.size() != other.hashTable.size())
    return false;

  size_t N = this->hashTable.size();

  for (size_t i = 0; i < N; i++) {
    if (this->hashTable[i].count != other.hashTable[i].count ||
        this->hashTable[i].keySum != other.hashTable[i].keySum ||
        this->hashTable[i].keyCheck != other.hashTable[i].keyCheck)
      return false;
  }

  return true;
}

std::string
IBLT::DumpTable() const
{
  std::ostringstream result;

  result << "count keySum keyCheckMatch\n";
  for (size_t i = 0; i < hashTable.size(); i++) {
    const HashTableEntry& entry = hashTable.at(i);
    result << entry.count << " " << entry.keySum << " ";
    result << ((MurmurHash3(N_HASHCHECK, ToVec(entry.keySum)) == entry.keyCheck) ||
              (entry.empty())? "true" : "false");
    result << "\n";
  }

  return result.str();
}

void
IBLT::appendToName(ndn::Name& name) const
{
  size_t N = hashTable.size();
  size_t unitSize = 32*3/8; // hard coding
  size_t tableSize = unitSize*N;

  std::vector <uint8_t> table(tableSize);

  for (uint i = 0; i < N; i++) {
    // table[i*12],   table[i*12+1], table[i*12+2], table[i*12+3] --> hashTable[i].count

    table[i*12]   = 0xFF & hashTable[i].count;
    table[i*12+1] = 0xFF & (hashTable[i].count >> 8);
    table[i*12+2] = 0xFF & (hashTable[i].count >> 16);
    table[i*12+3] = 0xFF & (hashTable[i].count >> 24);

    // table[i*12+4], table[i*12+5], table[i*12+6], table[i*12+7] --> hashTable[i].keySum

    table[i*12+4] = 0xFF & hashTable[i].keySum;
    table[i*12+5] = 0xFF & (hashTable[i].keySum >> 8);
    table[i*12+6] = 0xFF & (hashTable[i].keySum >> 16);
    table[i*12+7] = 0xFF & (hashTable[i].keySum >> 24);

    // table[i*12+8], table[i*12+9], table[i*12+10], table[i*12+11] --> hashTable[i].keyCheck

    table[i*12+8] = 0xFF & hashTable[i].keyCheck;
    table[i*12+9] = 0xFF & (hashTable[i].keyCheck >> 8);
    table[i*12+10] = 0xFF & (hashTable[i].keyCheck >> 16);
    table[i*12+11] = 0xFF & (hashTable[i].keyCheck >> 24);
  }

  name.appendNumber(table.size());
  name.append(table.begin(), table.end());
}

IBLT
IBLT::getIBLTFromName(size_t expectedNumEntries, size_t ibltSize,
                      const ndn::name::Component& ibltName) const
{
  std::vector<uint8_t> ibltValues(ibltName.begin() + getSize(ibltSize), ibltName.end());
  size_t N = ibltValues.size()/4;
  std::vector<uint32_t> values(N, 0);

  for (uint i = 0; i < 4*N; i += 4) {
    uint32_t t = (ibltValues[i+3] << 24) + (ibltValues[i+2] << 16) + (ibltValues[i+1] << 8) + ibltValues[i];
    values[i/4] = t;
  }

  return IBLT(expectedNumEntries, values);
}

} // namespace psync
