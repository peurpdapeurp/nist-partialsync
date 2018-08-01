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

#ifndef PSYNC_LOGIC_BASE_HPP
#define PSYNC_LOGIC_BASE_HPP

#include "iblt.hpp"
#include "bloom-filter.hpp"
#include "util.hpp"

#include <map>
#include <unordered_set>
#include <random>

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/time.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>

namespace psync {

typedef std::function<void(const std::vector<MissingDataInfo>)> UpdateCallback;

class LogicBase
{
protected:
  // Constructor for Full producer
  // since it has update call back to inform the user
  // Need another variable here to specify whether this producer
  // would entertain consumer hello or sync
  // NEED helloReplyFreshness here and below
  LogicBase(size_t expectedNumEntries,
            ndn::Face& face,
            const ndn::Name& syncPrefix,
            const ndn::Name& userPrefix);

  ~LogicBase();

  void
  addSyncNode(const std::string& prefix);

  void
  removeSyncNode(const std::string& prefix);

  /**
   * @brief Update m_prefixes and IBF with the given prefix and string
   *
   * We only add the prefix/seq if the prefix exists in m_prefixes and seq is newer than m_prefixes
   *
   * We remove already existing prefix/seq from IBF
   * (unless seq is zero because we don't insert zero seq into IBF)
   * Then we update m_prefix, m_prefix2hash, m_hash2prefix, and IBF
   *
   * @param prefix prefix of the update
   * @param seq sequence number of the update
   */
  void
  updateSeq(const std::string& prefix, uint32_t seq);

  /**
   * @brief Returns the current sequence number of the given prefix
   *
   * Might want to consider just returning the current sequence number from publishData.
   *
   * @param prefix prefix to get the sequence number of
   */
  uint32_t
  getSeq(const std::string& prefix) {
    return m_prefixes[prefix];
  }

  void
  sendApplicationNack(const ndn::Interest& interest);

  void
  printEntries(const IBLT& iblt, const std::string& ibltname);

  void
  onRegisterFailed(const ndn::Name& prefix, const std::string& msg) const;

protected:
  IBLT m_iblt;
  uint32_t m_expectedNumEntries;
  uint32_t m_threshold;

  std::map <std::string, uint32_t> m_prefixes; // prefix and sequence number
  std::map <std::string, uint32_t> m_prefix2hash;
  std::map <uint32_t, std::string> m_hash2prefix;

  ndn::Face& m_face;
  ndn::KeyChain m_keyChain;
  ndn::Scheduler m_scheduler;

  ndn::Name m_syncPrefix;
  ndn::Name m_userPrefix;

  ndn::time::milliseconds m_syncInterestLifetime;
  ndn::time::milliseconds m_syncReplyFreshness;

  std::mt19937 m_rng;
};

} // namespace psync

#endif // PSYNC_LOGIC_BASE_HPP
