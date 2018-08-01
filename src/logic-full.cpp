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

#include "logic-full.hpp"
#include "logging.hpp"

#include <boost/algorithm/string.hpp>

#include <iostream>
#include <cstring>
#include <limits>
#include <functional>

namespace psync {

_LOG_INIT(LogicFull);

// Why is this fixed, it is hash seed for murmur hash
static const size_t N_HASHCHECK = 11;

LogicFull::LogicFull(const size_t expectedNumEntries,
                     ndn::Face& face,
                     const ndn::Name& syncPrefix,
                     const ndn::Name& userPrefix,
                     const UpdateCallback& onUpdateCallBack,
                     ndn::time::milliseconds syncInterestLifetime,
                     ndn::time::milliseconds syncReplyFreshness)
  : LogicBase(expectedNumEntries, face, syncPrefix, userPrefix)
  , m_syncInterestLifetime(syncInterestLifetime)
  , m_syncReplyFreshness(syncReplyFreshness)
  , m_onUpdate(onUpdateCallBack)
  , m_outstandingInterestId(0)
  , m_jitter(-200, 200)
{
  _LOG_DEBUG("m_threshold " << m_threshold);
  addSyncNode(m_userPrefix.toUri());

  m_face.setInterestFilter(ndn::InterestFilter(m_syncPrefix).allowLoopback(false),
                           std::bind(&LogicFull::onSyncInterest, this, _1, _2),
                           std::bind(&LogicFull::onRegisterFailed, this, _1, _2));

  sendSyncInterest();
}

LogicFull::~LogicFull()
{
}

void
LogicFull::publishName(const std::string& prefix)
{
  if (m_prefixes.find(prefix) == m_prefixes.end()) {
    return;
  }

  uint32_t newSeq = m_prefixes[prefix] + 1;
  _LOG_INFO("Publish: "<< prefix << "/" << newSeq);

  updateSeq(prefix, newSeq);

  satisfyPendingInterests();
}

void
LogicFull::sendSyncInterest()
{
  // If we send two sync interest one after the other
  // since there is no new data in the network yet,
  // when data is available it may satisfy both of them
  if (m_outstandingInterestId != 0) {
    m_face.removePendingInterest(m_outstandingInterestId);
  }

  // Sync Interest format for full sync: /<sync-prefix>/<ourLatestIBF>
  ndn::Name syncInterestName = m_syncPrefix;

  // Append our latest IBF
  m_iblt.appendToName(syncInterestName);

  m_outstandingInterestName = syncInterestName;

  ndn::EventId eventId =
    m_scheduler.scheduleEvent(m_syncInterestLifetime / 2 +
                              ndn::time::milliseconds(m_jitter(m_rng)),
                              std::bind(&LogicFull::sendSyncInterest, this));
  m_scheduler.cancelEvent(m_scheduledSyncInterestId);
  m_scheduledSyncInterestId = eventId;

  ndn::Interest syncInterest(syncInterestName);
  syncInterest.setInterestLifetime(m_syncInterestLifetime);
  syncInterest.setMustBeFresh(true);

  syncInterest.setNonce(1);
  syncInterest.refreshNonce();

  m_outstandingInterestId = m_face.expressInterest(syncInterest,
                              std::bind(&LogicFull::onSyncData, this, _1, _2),
                              std::bind(&LogicFull::onSyncNack, this, _1, _2),
                              std::bind(&LogicFull::onSyncTimeout, this, _1));
  _LOG_DEBUG("sendFullSyncInterest lifetime: " << syncInterest.getInterestLifetime()
              << " nonce=" << syncInterest.getNonce()
              << ", hash: " << std::hash<std::string>{}(syncInterestName.toUri()));
}

void
LogicFull::onSyncInterest(const ndn::Name& prefixName, const ndn::Interest& interest)
{
  // PSync will get same sync interest multiple times from different faces
  // Because for the first interest on face 1, NFD will create a PitEntry and deliver to
  // multicast strategy after missing content store which will forward to this PSync's face
  // Then for the second interest on face 2 it will go the content store miss pipeline,
  // insert pit in record and hand over the interest to multicast strategy
  // which will simply forward the interest to all nexthops including this PSync's face

  _LOG_DEBUG("Full Sync Interest Received, Nonce: " << interest.getNonce()
             << ", hash: " << std::hash<std::string>{}(interest.getName().toUri()));

  // parse IBF
  ndn::Name interestName = interest.getName();
  size_t ibltSize = interestName.get(interestName.size()-2).toNumber();
  ndn::name::Component ibltName = interestName.get(interestName.size()-1);

  IBLT iblt = m_iblt.getIBLTFromName(m_expectedNumEntries, ibltSize, ibltName);

  _LOG_DEBUG("Equal? " << (m_iblt == iblt));

  IBLT diff = m_iblt - iblt;

  std::set<uint32_t> positive; //non-empty Positive means we have some elements that the others don't
  std::set<uint32_t> negative;

  if (!diff.listEntries(positive, negative)) {
    _LOG_DEBUG("Send Nack back - disabled");
    //this->sendApplicationNack(interest);
    return;
  }

  //assert((positive.size() == 1 && negative.size() == 1) || (positive.size() == 0 && negative.size() == 0));

  // WE DO NOT CHECK HERE THAT WHAT WE ARE SENDING BACK HAS A GREATER SEQUENCE NUMBER
  // ONLY THAT IT IS DIFFERENT? NOT SURE
  // WHY DOESN'T hash2prefix store prefixWithSeq

  // generate content in Sync reply
  std::string content;
  for (const auto& hash : positive) {
    std::string prefix = m_hash2prefix[hash];
    // Don't sync up sequence number zero
    // Only send back own data - disabled - prefix == m_userPrefix.toUri() &&
    if (m_prefixes[prefix] != 0) {
      // generate data
      content += prefix + " " + std::to_string(m_prefixes[prefix]) + "\n";
      //_LOG_DEBUG("Content: " << prefix << " " << std::to_string(m_prefixes[prefix]));
    }
  }

  if (positive.size() + negative.size() >= m_threshold || !content.empty()) {
    sendSyncData(interest.getName(), content);
    return;
  }

  // add the entry to the pending entry - if we don't have any new data now
  std::shared_ptr<PendingEntryInfo> entry = std::make_shared<PendingEntryInfo>(iblt);

  if (m_pendingEntries.find(interest.getName()) != m_pendingEntries.end()) {
    auto it = m_pendingEntries.find(interest.getName());
    m_scheduler.cancelEvent(it->second->expirationEvent);
  }

  // Insert does not replace the value if already there?

  m_pendingEntries.insert(std::make_pair<ndn::Name, std::shared_ptr<PendingEntryInfo>>
                          (ndn::Name(interest.getName()), std::make_shared<PendingEntryInfo>(iblt)));

  m_pendingEntries.find(interest.getName())->second->expirationEvent =
                          m_scheduler.scheduleEvent(
                            interest.getInterestLifetime(),
                            [=] {
                             _LOG_DEBUG("Erase Expired Pending Interest " << interest.getNonce());
                             m_pendingEntries.erase(interest.getName());
                            });
}

void
LogicFull::sendSyncData(const ndn::Name& name, const std::string& content)
{
  std::string c = content;
  boost::replace_all(c, "\n", ",");
  _LOG_DEBUG("Content:  " << c);

  _LOG_DEBUG("Checking if data will satisfy our own pending interest");

  // checking if our own interest got satisfied
  if (m_outstandingInterestName == name) {
    _LOG_DEBUG("Satisfied our own pending interest");
    // remove outstanding interest
    if (m_outstandingInterestId != 0) {
      _LOG_DEBUG("Removing our pending interest from face");
      m_face.removePendingInterest(m_outstandingInterestId);
      m_outstandingInterestId = 0;
      m_outstandingInterestName = ndn::Name("");
    }

    _LOG_DEBUG("Sending Sync Data");

    // Send data after removing pending sync interest on face
    ndn::Name syncDataName = name;
    m_iblt.appendToName(syncDataName);

    ndn::Data data;
    data.setName(syncDataName);
    data.setFreshnessPeriod(m_syncReplyFreshness);
    data.setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
    m_keyChain.sign(data);

    m_face.put(data);

    _LOG_TRACE("Renewing sync interest");
    sendSyncInterest();
  }
  else {
    _LOG_DEBUG("Sending Sync Data");
    ndn::Name syncDataName = name;
    m_iblt.appendToName(syncDataName);

    ndn::Data data;
    data.setName(syncDataName);
    data.setFreshnessPeriod(m_syncReplyFreshness);
    data.setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
    m_keyChain.sign(data);

    m_face.put(data);
  }
}

void
LogicFull::onSyncData(const ndn::Interest& interest, const ndn::Data& data)
{
  _LOG_DEBUG("<<< On Sync Data"); // for interest: " << interest.getName());

  deletePendingInterests(interest.getName());

  ndn::Name syncDataName = data.getName();

  std::string content(reinterpret_cast<const char*>(data.getContent().value()),
                      data.getContent().value_size());

  std::string c = content;
  boost::replace_all(c, "\n", ",");
  _LOG_DEBUG("Sync Data:  " << c);

  std::vector<MissingDataInfo> updates;
  std::vector<std::string> prefixList;
  std::vector<std::string> prefixSplit;

  boost::split(prefixList, content, boost::is_any_of("\n"));

  for (const std::string& data : prefixList) {
    //_LOG_DEBUG("t" << data << "t");
    if (data == "") {
      continue;
    }
    boost::split(prefixSplit, data, boost::is_any_of(" "));
    std::string prefix = prefixSplit.at(0);
    uint32_t seq = 1;
    try {
      seq = std::stoi(prefixSplit.at(1));
    } catch (const std::exception& e) {
      _LOG_DEBUG("Cannot convert " << prefixSplit.at(1) << " to integer");
      _LOG_DEBUG("Error1: " << e.what());
    }

    if (m_prefixes.find(prefix) == m_prefixes.end() || m_prefixes[prefix] < seq) {
      // deletePendingSyncInterest and Update seq here before pushing update
      // so that we don't need +1 here?
      // Think of the case where applications forces their sequence numbers (not supported yet - but still)
      updates.push_back(MissingDataInfo(prefix, m_prefixes[prefix] + 1, seq));
      updateSeq(prefix, seq);
      // We should not call satisfyPendingSyncInterests here because we just
      // got data and deleted pending interest by calling deletePendingFullSyncInterests
      // But we might have interests not matching to this interest that might not have deleted
      // from pending sync interest
    }
  }

  // We just got the data, so send a new sync interest
  if (!updates.empty()) {
    m_onUpdate(updates);
    _LOG_TRACE("Renewing sync interest");
    sendSyncInterest();
  }
  else {
    _LOG_DEBUG("No new update, interest: " << interest.getNonce() << " " << std::hash<std::string>{}(interest.getName().toUri()));
  }

  //else {
    // This is commented because there can be a situation where we get an update for /prefix with seq=1
    // but we already have /prefix with seq=2. The other side cannot distinguish (need to add a test for this?)
    // that we have a greater sequence
    // So we don't put this update in our IBF and send the same sync interest that hits CS again and again
    // resulting in high CPU usage
    // This does not make full sync incorrect - just a bit slow
    // We can make it scheduled after higher than data freshness to avoid this situation - but might be faster
    // to just follow the sync schedule (1 sec, data freshness is also 1 sec currently)
    /*ndn::time::steady_clock::Duration after(m_jitter(m_rng));
    _LOG_DEBUG("Reschedule sync interest after: " << after);
    ndn::EventId eventId = m_scheduler.scheduleEvent(after, std::bind(&LogicFull::sendSyncInterest, this));

    m_scheduler.cancelEvent(m_scheduledSyncInterestId);
    m_scheduledSyncInterestId = eventId;*/
  //}
}

void
LogicFull::onSyncTimeout(const ndn::Interest& interest)
{
  _LOG_DEBUG("On full sync timeout " << interest.getNonce());
}

void
LogicFull::onSyncNack(const ndn::Interest& interest, const ndn::lp::Nack& nack)
{
  _LOG_TRACE("received Nack with reason " << nack.getReason()
             << " for Interest with Nonce: " << interest.getNonce());
}

void
LogicFull::satisfyPendingInterests()
{
  _LOG_DEBUG("Satisfying full sync interest: " << m_pendingEntries.size());
  std::vector <ndn::Name> prefixToErase(m_pendingEntries.size());

  // Satisfy pending interests from other producers
  // If you put this to const auto& prefixToErase.push_back will get a
  // segfault (heap-free-after use error)
  for (auto pendingInterest : m_pendingEntries) {
    // go through each pendingEntries
    std::shared_ptr<PendingEntryInfo> entry = pendingInterest.second;
    IBLT diff = m_iblt - entry->iblt;
    std::set<uint32_t> positive;
    std::set<uint32_t> negative;

    _LOG_DEBUG("Equal? " << (m_iblt == entry->iblt));

    if (!diff.listEntries(positive, negative)) {
      _LOG_DEBUG("Send Nack disabled, continue");
      //this->sendApplicationNack(pendingInterest.first);
      prefixToErase.push_back(pendingInterest.first);
      m_scheduler.cancelEvent(entry->expirationEvent);
      continue;
    }

    // Is this correct/necessary? I think so because in onSyncInterest we check that if content
    // is not empty only then we send it, but here there is no check here since this function is called
    // from publishData or potentially from onSyncData
    if (positive.size() == 0 && negative.size() == 0) {
      _LOG_DEBUG("No difference between our IBF and pending sync interest's IBF");
      continue;
    }

    // We need to do go over hash and not just use prefix because
    // we don't send sync data upon receiving sync data from other side
    std::string content;
    for (const auto& hash : positive) {
      std::string prefix = m_hash2prefix[hash];
      // Don't sync up sequence number zero
      // Only send back own data - disabled - prefix == m_userPrefix.toUri() &&
      if (m_prefixes[prefix] != 0) {
        // generate data
        content += prefix + " " + std::to_string(m_prefixes[prefix]) + "\n";
        //_LOG_DEBUG("Content: " << prefix << " " << std::to_string(m_prefixes[prefix]));
      }
    }

    if (positive.size() + negative.size() >= m_threshold || !content.empty()) {
      sendSyncData(pendingInterest.first, content);
      prefixToErase.push_back(pendingInterest.first);
      m_scheduler.cancelEvent(entry->expirationEvent);
    }
  }

  for (auto pte : prefixToErase) {
    m_pendingEntries.erase(pte);
  }
}

void
LogicFull::deletePendingInterests(const ndn::Name& interestName) {
  std::vector <ndn::Name> prefixToErase;
  for (auto pendingInterest : m_pendingEntries) {
    // Check that pending interest match to the data
    // received in Full onSyncData
    if (pendingInterest.first == interestName) {
      // go through each pendingEntries
      std::shared_ptr<PendingEntryInfo> entry = pendingInterest.second;

      prefixToErase.push_back(pendingInterest.first);
      m_scheduler.cancelEvent(entry->expirationEvent);
    }
  }

  if (prefixToErase.size() == 0) {
    _LOG_DEBUG("No matching pending sync interest to delete");
    return;
  }

  for (auto pte : prefixToErase) {
    _LOG_DEBUG("Pending interest deleted");
    m_pendingEntries.erase(pte);
  }
}

} // namespace psync
