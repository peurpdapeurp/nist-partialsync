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

#include "logic-base.hpp"
#include "logging.hpp"

#include <boost/algorithm/string.hpp>

#include <iostream>
#include <cstring>
#include <limits>
#include <functional>

namespace psync {

_LOG_INIT(LogicBase);

// Why is this fixed, it is hash seed for murmur hash
static const size_t N_HASHCHECK = 11;

LogicBase::LogicBase(size_t expectedNumEntries,
                     ndn::Face& face,
                     const ndn::Name& syncPrefix,
                     const ndn::Name& userPrefix)
  : m_iblt(expectedNumEntries)
  , m_expectedNumEntries(expectedNumEntries)
  , m_threshold(expectedNumEntries/2)
  , m_face(face)
  , m_scheduler(m_face.getIoService())
  , m_syncPrefix(syncPrefix)
  , m_userPrefix(userPrefix)
  , m_rng(std::random_device{}())
{
}

LogicBase::~LogicBase()
{
  _LOG_DEBUG("Logic destructor called");
  m_scheduler.cancelAllEvents();
  m_face.shutdown();
}

void
LogicBase::addSyncNode(const std::string& prefix)
{
  if (m_prefixes.find(prefix) == m_prefixes.end()) {
    m_prefixes[prefix] = 0;
  }
}

void
LogicBase::removeSyncNode(const std::string& prefix)
{
  if (m_prefixes.find(prefix) != m_prefixes.end()) {
    uint32_t seqNo = m_prefixes[prefix];
    m_prefixes.erase(prefix);
    std::string prefixWithSeq = prefix + "/" + std::to_string(seqNo);
    uint32_t hash = m_prefix2hash[prefixWithSeq];
    m_prefix2hash.erase(prefixWithSeq);
    m_hash2prefix.erase(hash);
    m_iblt.erase(hash);
  }
}

void
LogicBase::updateSeq(const std::string& prefix, uint32_t seq)
{
  _LOG_DEBUG("UpdateSeq: " << prefix << " " << seq);
  if (m_prefixes.find(prefix) != m_prefixes.end() && m_prefixes[prefix] >= seq) {
    _LOG_WARN("UpdateSeq: returning!! m_prefixes[prefix]: " << m_prefixes[prefix]);
    return;
  }

  // Delete the last sequence prefix from the iblt
  // Because we don't insert zeroth prefix in IBF so no need to delete that
  if (m_prefixes.find(prefix) != m_prefixes.end() && m_prefixes[prefix] != 0) {
    uint32_t hash = m_prefix2hash[prefix + "/" + std::to_string(m_prefixes[prefix])];
    m_prefix2hash.erase(prefix + "/" + std::to_string(m_prefixes[prefix]));
    m_hash2prefix.erase(hash);
    m_iblt.erase(hash);
  }

  // Insert the new seq no
  m_prefixes[prefix] = seq;
  std::string prefixWithSeq = prefix + "/" + std::to_string(m_prefixes[prefix]);
  uint32_t newHash = MurmurHash3(N_HASHCHECK, ParseHex(prefixWithSeq));
  m_prefix2hash[prefixWithSeq] = newHash;
  m_hash2prefix[newHash] = prefix;
  m_iblt.insert(newHash);
}

void
LogicBase::sendApplicationNack(const ndn::Interest& interest)
{
  std::string content = "NACK 0";
  ndn::Data data;
  data.setName(interest.getName());
  data.setFreshnessPeriod(m_syncReplyFreshness);
  data.setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
  m_keyChain.sign(data);
  m_face.put(data);
}

void
LogicBase::onRegisterFailed(const ndn::Name& prefix, const std::string& msg) const
{
  _LOG_ERROR("LogicFull::onRegisterFailed " << prefix << " " << msg);
}

} // namespace psync