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

#include "iblt.hpp"
#include "util.hpp"
#include <iostream>

#include <boost/test/unit_test.hpp>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/interest.hpp>

namespace psync {

using namespace ndn;

BOOST_AUTO_TEST_SUITE(TestIBLT)

BOOST_AUTO_TEST_CASE(Equal)
{
  int size = 10;

  IBLT iblt1(size);
  IBLT iblt2(size);
  BOOST_CHECK(iblt1 == iblt2);

  std::string prefix = "/test/memphis/" + std::to_string(1);
  uint32_t newHash = MurmurHash3(11, ParseHex(prefix));
  iblt1.insert(newHash);
  iblt2.insert(newHash);

  BOOST_CHECK(iblt1 == iblt2);

  Name ibfName1("sync"), ibfName2("sync");
  iblt1.appendToName(ibfName1);
  iblt2.appendToName(ibfName2);
  BOOST_CHECK_EQUAL(ibfName1, ibfName2);
}

BOOST_AUTO_TEST_CASE(EncodeDecodeTest)
{
  int size = 10;

  IBLT iblt(size);
  std::string prefix = "/test/memphis/" + std::to_string(1);
  uint32_t newHash = MurmurHash3(11, ParseHex(prefix));
  iblt.insert(newHash);

  Name ibltName("sync");
  iblt.appendToName(ibltName);

  // May be make this static function?
  IBLT rcvd = iblt.getIBLTFromName(size,
                                   ibltName.get(ibltName.size()-2).toNumber(),
                                   ibltName.get(ibltName.size()-1));
}

BOOST_AUTO_TEST_CASE(CopyInsertErase)
{
  int size = 10;

  IBLT iblt1(size);

  std::string prefix = "/test/memphis/" + std::to_string(1);
  uint32_t hash1 = MurmurHash3(11, ParseHex(prefix));
  iblt1.insert(hash1);

  IBLT iblt2(iblt1);

  iblt1.erase(hash1);
  prefix = "/test/memphis/" + std::to_string(5);
  uint32_t hash5 = MurmurHash3(11, ParseHex(prefix));
  iblt1.insert(hash5);

  iblt2.erase(hash1);
  prefix = "/test/memphis/" + std::to_string(2);
  uint32_t hash3 = MurmurHash3(11, ParseHex(prefix));
  iblt2.insert(hash3);

  iblt2.erase(hash3);
  iblt2.insert(hash5);

  BOOST_CHECK(iblt1 == iblt2);
}

BOOST_AUTO_TEST_CASE(HigherSeqTest)
{
  // The case where we can't recognize if the rcvd IBF has higher sequence number
  // Relevant to full sync case
  int size = 10;

  IBLT ownIBF(size);
  IBLT rcvdIBF(size);

  std::string prefix = "/test/memphis/" + std::to_string(3);
  uint32_t hash1 = MurmurHash3(11, ParseHex(prefix));
  ownIBF.insert(hash1);

  std::string prefix2 = "/test/memphis/" + std::to_string(4);
  uint32_t hash2 = MurmurHash3(11, ParseHex(prefix2));
  rcvdIBF.insert(hash2);

  IBLT diff = ownIBF - rcvdIBF;
  std::set<uint32_t> positive;
  std::set<uint32_t> negative;

  BOOST_CHECK_EQUAL(diff.listEntries(positive, negative), true);
  BOOST_CHECK(*positive.begin() == hash1);
  // Should be zero but can't recognize
  // BOOST_CHECK_EQUAL(positive.size(), 0);
}

BOOST_AUTO_TEST_CASE(Difference)
{
  int size = 10;

  IBLT ownIBF(size);

  IBLT rcvdIBF = ownIBF;

  IBLT diff = ownIBF - rcvdIBF;

  std::set<uint32_t> positive; //non-empty Positive means we have some elements that the others don't
  std::set<uint32_t> negative;

  BOOST_CHECK_EQUAL(diff.listEntries(positive, negative), true);
  BOOST_CHECK_EQUAL(positive.size(), 0);
  BOOST_CHECK_EQUAL(negative.size(), 0);

  std::string prefix = "/test/memphis/" + std::to_string(1);
  uint32_t newHash = MurmurHash3(11, ParseHex(prefix));
  ownIBF.insert(newHash);

  diff = ownIBF - rcvdIBF;
  BOOST_CHECK_EQUAL(diff.listEntries(positive, negative), true);
  BOOST_CHECK_EQUAL(positive.size(), 1);
  BOOST_CHECK_EQUAL(negative.size(), 0);

  prefix = "/test/csu/" + std::to_string(1);
  newHash = MurmurHash3(11, ParseHex(prefix));
  rcvdIBF.insert(newHash);

  diff = ownIBF - rcvdIBF;
  BOOST_CHECK_EQUAL(diff.listEntries(positive, negative), true);
  BOOST_CHECK_EQUAL(positive.size(), 1);
  BOOST_CHECK_EQUAL(negative.size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace psync