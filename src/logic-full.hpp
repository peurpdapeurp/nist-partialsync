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

#ifndef PSYNC_LOGIC_FULL_HPP
#define PSYNC_LOGIC_FULL_HPP

#include "iblt.hpp"
#include "bloom-filter.hpp"
#include "util.hpp"
#include "logic-base.hpp"

#include <map>
#include <unordered_set>
#include <random>

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/time.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>

namespace psync {

struct PendingEntryInfo {
  PendingEntryInfo(const IBLT& iblt)
  : iblt(iblt)
  {}

  IBLT iblt;
  ndn::EventId expirationEvent;
};

typedef std::function<void(const std::vector<MissingDataInfo>)> UpdateCallback;

class LogicFull : public LogicBase
{
public:
  // Constructor for Full producer
  // since it has update call back to inform the user
  // Need another variable here to specify whether this producer
  // would entertain consumer hello or sync
  // NEED helloReplyFreshness here and below
  LogicFull(size_t expectedNumEntries,
            ndn::Face& face,
            const ndn::Name& syncPrefix,
            const ndn::Name& userPrefix,
            const UpdateCallback& onUpdateCallBack,
            ndn::time::milliseconds syncInterestLifetime,
            ndn::time::milliseconds syncReplyFreshness);

  ~LogicFull();

  void
  publishName(const std::string& prefix);

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

private:
  /**
   * @brief Send sync interest for full synchronization
   *
   * Forms the interest name: /<sync-prefix>/full-sync/<own-IBF>
   * Cancels any pending sync interest we sent earlier on the face
   * Sends the sync interest
   */
  void
  sendSyncInterest();

  /**
   * @brief Process sync interest from other parties
   *
   * Extract IBF from the @p interest
   * Get differences b/w our IBF and this IBF
   *   If we cannot get the differences successfully then send application level Nack (Disabled currently)
   *
   * If have some things in our IBF that the other side does not have, reply with the content
   * or if # of new data items is greater than threshold then reply with whatever content we have that other side don't
   * and return
   * Otherwise add the sync interest into a map with interest name as key and PendingEntryInfo as value
   * (PendingEntryInfo contains BloomFilter, IBF, and ExpirationEvent - BloomFilter is not used)
   * Need a unit test to make sure that this^ is done correctly
   *
   * @param prefixName prefix for which we registered
   * @param interest the interest we got
   */
  void
  onSyncInterest(const ndn::Name& prefixName, const ndn::Interest& interest);

  void
  sendSyncData(const ndn::Name& name, const std::string& content);

  /**
   * @brief Process sync data
   *
   * Delete pending full sync interests that match the interest name
   *   This is because any pending sync interest with @p interest name would have
   *   been satisfied once NFD got the data
   *
   * For each prefix/seq in data content
   *   Check that we don't already have the prefix/seq and updateSeq(prefix, seq)
   *
   * Notify the client about the updates
   * sendSyncInterest because the last one was satisfied by the incoming data
   *
   * @param interest interest for which we got the data
   * @param data the data packet we got
   */
  void
  onSyncData(const ndn::Interest& interest, const ndn::Data& data);

  /**
   * @brief Process sync timeout
   *
   * sendSyncInterest immediately
   *
   * @param interest interest for which we got the timeout for
   */
  void
  onSyncTimeout(const ndn::Interest& interest);

  /**
   * @brief Process sync nack
   *
   * sendSyncInterest after 500 ms + 10-50 ms jitter
   *
   * @param interest interest for which we got the timeout for
   * @param nack nack packet
   */
  void
  onSyncNack(const ndn::Interest& interest, const ndn::lp::Nack& nack);

  /**
   * @brief Process sync timeout
   *
   * For pending sync interests sI:
   *     if IBF of sI has any difference from our own IBF:
   *          send data back for @p prefix latest sequence
   *
   * Remove all pending sync interests (current implementation - will change)
   * @param prefix
   */
  void
  satisfyPendingInterests();

  void
  deletePendingInterests(const ndn::Name& interestName);

private:
  // std::bad_alloc if we do not use shared_pointer for PendingEntryInfo
  // on map insert
  // Need a new dedicated class like ChronoSync's interest table to manage these?
  std::map <ndn::Name, std::shared_ptr<PendingEntryInfo>> m_pendingEntries;

  ndn::time::milliseconds m_syncInterestLifetime;
  ndn::time::milliseconds m_syncReplyFreshness;

  UpdateCallback m_onUpdate;
  const ndn::PendingInterestId* m_outstandingInterestId;
  ndn::EventId m_scheduledSyncInterestId;

  std::uniform_int_distribution<> m_jitter;

  ndn::Name m_outstandingInterestName;
};

} // namespace psync

#endif // PSYNC_LOGIC_FULL_HPP
