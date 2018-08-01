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

#include "logic-partial.hpp"
#include "logging.hpp"
#include "util.hpp"

#include <iostream>
#include <cstring>
#include <limits>

namespace psync {

_LOG_INIT(LogicPartial);

static const size_t N_HASHCHECK = 11;

LogicPartial::LogicPartial(size_t expectedNumEntries,
                           ndn::Face& face,
                           const ndn::Name& syncPrefix,
                           const ndn::Name& userPrefix,
                           ndn::time::milliseconds helloReplyFreshness,
                           ndn::time::milliseconds syncReplyFreshness)
: LogicBase(expectedNumEntries, face, syncPrefix, userPrefix)
, m_helloReplyFreshness(helloReplyFreshness)
, m_syncReplyFreshness(syncReplyFreshness)
{
  m_face.registerPrefix(m_syncPrefix,
    [this] (const ndn::Name& syncPrefix) {
      m_face.setInterestFilter(ndn::InterestFilter((ndn::Name(m_syncPrefix)).append("hello")).allowLoopback(false),
                               std::bind(&LogicPartial::onHelloInterest, this, _1, _2));

      m_face.setInterestFilter(ndn::InterestFilter((ndn::Name(m_syncPrefix)).append("sync")).allowLoopback(false),
                               std::bind(&LogicPartial::onSyncInterest, this, _1, _2));
    },
    std::bind(&LogicPartial::onRegisterFailed, this, _1, _2));
}

LogicPartial::~LogicPartial()
{
}

void
LogicPartial::publishName(const std::string& prefix)
{
  if (m_prefixes.find(prefix) == m_prefixes.end()) {
    return;
  }

  uint32_t newSeq = m_prefixes[prefix] + 1;

  _LOG_INFO("Publish: "<< prefix << "/" << newSeq);

  try {
    updateSeq(prefix, m_prefixes[prefix] + 1);
  } catch (const std::exception& e) {
    _LOG_ERROR("Error: " << e.what());
  }
  satisfyPendingSyncInterests(prefix);
}

void
LogicPartial::onHelloInterest(const ndn::Name& prefix, const ndn::Interest& interest)
{
  //!! The way our segment publisher/fetcher hello is set means hello can never be answered
  // from the content store

  _LOG_DEBUG("Hello Interest Received " << prefix.toUri() << " Nonce " << interest.getNonce());

  std::string content = "";
  size_t i = 0;
  for (const auto& p : m_prefixes) {
    if (i++ == m_prefixes.size()-1) {
      content += p.first;
    }
    else {
      content += p.first + "\n";
    }
  }
  _LOG_DEBUG("sending content p: " << content);

  ndn::Name segmentPrefix = prefix;
  m_iblt.appendToName(segmentPrefix);

  sendFragmentedData(segmentPrefix, content);
}

void
LogicPartial::sendFragmentedData(const ndn::Name& segmentPrefix, const std::string& content)
{
  ndn::EncodingBuffer buffer;
  buffer.prependByteArray(reinterpret_cast<const uint8_t*>(content.c_str()), content.size());

  const uint8_t* rawBuffer = buffer.buf();
  const uint8_t* segmentBegin = rawBuffer;
  const uint8_t* end = rawBuffer + buffer.size();

  uint64_t segmentNo = 0;
  do {
    const uint8_t* segmentEnd = segmentBegin + ndn::MAX_NDN_PACKET_SIZE/2;
    if (segmentEnd > end) {
      segmentEnd = end;
    }

    ndn::Name segmentName(segmentPrefix);
    segmentName.appendSegment(segmentNo);

    std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(segmentName);
    data->setContent(segmentBegin, segmentEnd - segmentBegin);
    data->setFreshnessPeriod(m_helloReplyFreshness);

    segmentBegin = segmentEnd;
    if (segmentBegin >= end) {
      data->setFinalBlock(segmentName[-1]);
    }

    m_keyChain.sign(*data);
    m_face.put(*data);

    _LOG_DEBUG("Sending data " << *data);

    ++segmentNo;
  } while (segmentBegin < end);
}

void
LogicPartial::onSyncInterest(const ndn::Name& prefix, const ndn::Interest& interest)
{
  _LOG_DEBUG("Sync Interest Received, Nonce: " << interest.getNonce()
              << " " << std::hash<std::string>{}(interest.getName().toUri()));

  // parser BF and IBLT Not finished yet
  ndn::Name interestName = interest.getName();
  _LOG_DEBUG(interestName.get(interestName.size()-4));
  std::size_t bfSize = interestName.get(interestName.size()-4).toNumber();
  ndn::name::Component bfName = interestName.get(interestName.size()-3);
  std::size_t ibltSize = interestName.get(interestName.size()-2).toNumber();
  ndn::name::Component ibltName = interestName.get(interestName.size()-1);

  bloom_parameters opt;
  opt.projected_element_count = interestName.get(interestName.size()-6).toNumber();
  //_LOG_DEBUG("Elemen count of BF: " << opt.projected_element_count);
  //_LOG_DEBUG("Probab of BF: " << interestName.get(interestName.size()-5).toNumber()/1000.);
  opt.false_positive_probability = interestName.get(interestName.size()-5).toNumber()/1000.;
  opt.compute_optimal_parameters();
  bloom_filter bf(opt);
  bf.setTable(std::vector <uint8_t>(bfName.begin() + getSize(bfSize), bfName.end()));

  std::vector <uint8_t> testTable(bfName.begin(), bfName.end());

  std::vector <uint8_t> ibltValues(ibltName.begin() + getSize(ibltSize), ibltName.end());
  std::size_t N = ibltValues.size()/4;
  std::vector <uint32_t> values(N, 0);

  for (uint i = 0; i < 4*N; i += 4) {
    uint32_t t = (ibltValues[i+3] << 24) + (ibltValues[i+2] << 16) + (ibltValues[i+1] << 8) + ibltValues[i];
    values[i/4] = t;
  }

  // get the difference
  IBLT iblt(m_expectedNumEntries, values);
  IBLT diff = m_iblt - iblt;
  std::set<uint32_t> positive; //non-empty Positive means we have some elements that the others don't
  std::set<uint32_t> negative;

  //_LOG_DEBUG("Diff List entries: " << diff.listEntries(positive, negative));

  //printEntries(m_iblt, "MyIBF");
  //printEntries(iblt, "IBF received");
  //printEntries(diff, "Difference:");
  //_LOG_DEBUG("Difference size: " << diff.getHashTable().size());
  _LOG_DEBUG("Num elements in IBF: " << m_prefixes.size());

  bool peel = diff.listEntries(positive, negative);

  _LOG_DEBUG("diff.listEntries: " << peel);

  /*if (!peel) {
    _LOG_DEBUG("Send Nack back");
    this->sendNack(interest);
    return;
  }*/

  //assert((positive.size() == 1 && negative.size() == 1) || (positive.size() == 0 && negative.size() == 0));

  // generate content in Sync reply
  std::string content;
  _LOG_DEBUG("Size of positive set " << positive.size());
  _LOG_DEBUG("Size of negative set " << negative.size());
  for (const auto& hash : positive) {
    std::string prefix = m_hash2prefix[hash];
    if (bf.contains(prefix)) {
      // generate data
      content += prefix + " " + std::to_string(m_prefixes[prefix]) + "\n";
      _LOG_DEBUG("Content: " << prefix << " " << std::to_string(m_prefixes[prefix]));
    }
  }

  _LOG_DEBUG("m_threshold: " << m_threshold << " Total: " << positive.size() + negative.size());

  if (positive.size() + negative.size() >= m_threshold || !content.empty()) {
    //printEntries(m_iblt, "onsyncinterest, m_iblt");
    //printEntries(iblt, "onsyncinterest, iblt");
    //printEntries(diff, "onsyncinterest, diff");

    // send back data
    //std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>();
    ndn::Name syncDataName = interest.getName();
    m_iblt.appendToName(syncDataName);

    /*data->setName(syncDataName);
    data->setFreshnessPeriod(m_syncReplyFreshness);
    data->setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
    m_keyChain.sign(*data);

    _LOG_DEBUG("Send Data back ");

    m_face.put(*data);*/
    sendFragmentedData(syncDataName, content);

    return;
  }

  // add the entry to the pending entry - if we don't have any new data now
  std::shared_ptr<PendingEntryInfo> entry = std::make_shared<PendingEntryInfo>(bf, iblt);
  //PendingEntryInfo entry(bf, iblt);

  // Because insert member function will have no effect if the key is already present in the map
  if (m_pendingEntries.find(interest.getName()) != m_pendingEntries.end()) {
    auto it = m_pendingEntries.find(interest.getName());
    m_scheduler.cancelEvent(it->second->expirationEvent);
  }

   //_LOG_DEBUG("The map contains " << m_pendingEntries.size () << " key-value pairs. ");

  try {
    m_pendingEntries.insert(std::make_pair(interest.getName(), entry));
  } catch (const std::exception& e) {
    _LOG_DEBUG("error: " << e.what());
    std::cout << "error: " << e.what() << std::endl;
    exit(1);
  }

  m_pendingEntries.find(interest.getName())->second->expirationEvent =
                             m_scheduler.scheduleEvent(
                               interest.getInterestLifetime(),
                               [this, interest] {
                                 _LOG_DEBUG("Erase Pending Interest " << interest.getNonce());
                                  m_pendingEntries.erase(interest.getName());
                               });
}

void
LogicPartial::satisfyPendingSyncInterests(const std::string& prefix) {
  _LOG_DEBUG("size of pending interest: " << m_pendingEntries.size());
  std::vector <ndn::Name> prefixToErase;

  // Satisfy pending interests
  for (const auto& pendingInterest : m_pendingEntries) {
    _LOG_DEBUG("---------------------"
               << std::hash<std::string>{}(pendingInterest.first.toUri())
               << "--------------------");
    // go through each pendingEntries
    //PendingEntryInfo entry = pendingInterest.second;
    std::shared_ptr<PendingEntryInfo> entry = pendingInterest.second;
    if (!entry) {continue;}
    IBLT diff = m_iblt - entry->iblt;
    std::set<uint32_t> positive;
    std::set<uint32_t> negative;

    bool peel = diff.listEntries(positive, negative);

    //printEntries(m_iblt, "MyIBF");
    //printEntries(entry->iblt, "pending IBF");
    //printEntries(diff, "Diff");

    _LOG_TRACE("diff.listEntries: " << peel);

    _LOG_TRACE("Num elements in IBF: " << m_prefixes.size());
    _LOG_TRACE("m_threshold: " << m_threshold << " Total: " << positive.size() + negative.size());

    if (!peel) {
      _LOG_DEBUG("Cannot peel all the difference between pending IBF and our current IBF");
      _LOG_DEBUG("Deleted pending sync interest");
      //this->sendNack(pendingInterest.first);
      prefixToErase.push_back(pendingInterest.first);
      m_scheduler.cancelEvent(entry->expirationEvent);
      continue;
    }

    if (entry->bf.contains(prefix) || positive.size() + negative.size() >= m_threshold) {
      std::string syncContent;
      if (entry->bf.contains(prefix)) {
         _LOG_DEBUG("sending sync content " << prefix << " " << std::to_string(m_prefixes[prefix]));
         syncContent = prefix + " " + std::to_string(m_prefixes[prefix]);
      }
      else {
        _LOG_DEBUG("Sending with empty content so that consumer's IBF is updated since the threshold was crossed");
      }

      // generate sync data and cancel the scheduler
      ndn::Name syncDataName = pendingInterest.first;
      m_iblt.appendToName(syncDataName);

      sendFragmentedData(syncDataName, syncContent);

      /*std::shared_ptr<ndn::Data> syncData = std::make_shared<ndn::Data>();
      syncData->setName(syncDataName);
      syncData->setContent(reinterpret_cast<const uint8_t*>(syncContent.c_str()), syncContent.length());
      syncData->setFreshnessPeriod(m_syncReplyFreshness);
      m_keyChain.sign(*syncData);
      _LOG_DEBUG(*syncData);
      m_face.put(*syncData);*/

      prefixToErase.push_back(pendingInterest.first);
      m_scheduler.cancelEvent(entry->expirationEvent);
    }
  }

  for (const auto& pte : prefixToErase) {
    m_pendingEntries.erase(pte);
  }
}

} // namespace psync
