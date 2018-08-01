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

#ifndef PSYNC_LOGIC_PARTIAL_HPP
#define PSYNC_LOGIC_PARTIAL_HPP

#include "iblt.hpp"
#include "bloom-filter.hpp"
#include "logic-base.hpp"

#include <map>
#include <unordered_set>

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/time.hpp>
#include <ndn-cxx/security/key-chain.hpp>

namespace psync {

struct PendingEntryInfo {
  PendingEntryInfo(const bloom_filter& bf, const IBLT& iblt)
  : bf(bf)
  , iblt(iblt)
  , expirationEvent(0)
  {}

  bloom_filter bf;
  IBLT iblt;
  ndn::EventId expirationEvent;
};

class LogicPartial : public LogicBase
{
public:
  LogicPartial(size_t expectedNumEntries,
               ndn::Face& face,
               const ndn::Name& syncPrefix,
               const ndn::Name& userPrefix,
               ndn::time::milliseconds helloReplyFreshness,
               ndn::time::milliseconds syncReplyFreshness);

  ~LogicPartial();

  void
  publishName(const std::string& prefix);

  uint32_t
  getSeq(std::string prefix) {
    return m_prefixes[prefix];
  }

  bool
  isSyncNode(std::string prefix) {
    if (m_prefixes.find(prefix) == m_prefixes.end()) {
      return false;
    }
    return true;
  }

private:
  void
  satisfyPendingSyncInterests(const std::string& prefix);

  void
  onHelloInterest(const ndn::Name& prefix, const ndn::Interest& interest);

  void
  onSyncInterest(const ndn::Name& prefix, const ndn::Interest& interest);

  void
  sendFragmentedData(const ndn::Name& segmentPrefix, const std::string& content);

private:
  //std::map <ndn::Name, PendingEntryInfo> m_pendingEntries;
  std::map <ndn::Name, std::shared_ptr<PendingEntryInfo>> m_pendingEntries;

  ndn::time::milliseconds m_helloReplyFreshness;
  ndn::time::milliseconds m_syncReplyFreshness;
};

} // namespace psync

#endif // PSYNC_LOGIC_PARTIAL_HPP
