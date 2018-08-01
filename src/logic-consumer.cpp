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

#include "logic-consumer.hpp"
#include "logging.hpp"

#include <ndn-cxx/util/time.hpp>
#include <ctime>

#include <boost/algorithm/string.hpp>

namespace psync {

_LOG_INIT(LogicConsumer);

LogicConsumer::LogicConsumer(ndn::Name& prefix,
                             ndn::Face& face,
                             RecieveHelloCallback& onRecieveHelloData,
                             UpdateCallback& onUpdate,
                             unsigned int count,
                             double false_positve)
: m_syncPrefix(prefix)
, m_face(face)
, m_onRecieveHelloData(onRecieveHelloData)
, m_onUpdate(onUpdate)
, m_count(count)
, m_false_positive(false_positve)
, m_suball(false_positve == 0.001 && m_count == 1)  //subscribe to all data streams - a hack
, m_helloSent(false)
, m_scheduler(m_face.getIoService())
, m_randomGenerator(static_cast<unsigned int>(std::time(0)))
, m_rangeUniformRandom(m_randomGenerator, boost::uniform_int<>(100,500))
{
  bloom_parameters opt;
  opt.false_positive_probability = m_false_positive;
  opt.projected_element_count = m_count;
  opt.compute_optimal_parameters();
  m_bf = bloom_filter(opt);
}

LogicConsumer::~LogicConsumer()
{
  m_scheduler.cancelAllEvents();
  m_face.shutdown();
}

void
LogicConsumer::stop()
{
  m_face.shutdown();
}

void
LogicConsumer::sendHelloInterest()
{
  ndn::Name helloInterestName = m_syncPrefix;
  helloInterestName.append("hello");

  ndn::Interest helloInterest(helloInterestName);
  helloInterest.setInterestLifetime(ndn::time::milliseconds(4000));
  helloInterest.setMustBeFresh(true);

  _LOG_DEBUG("Send Hello Interest " << helloInterest << " Nonce " << helloInterest.getNonce() <<
             " mustbefresh: " << helloInterest.getMustBeFresh());

  m_face.expressInterest(helloInterest,
                         bind(&LogicConsumer::onHelloData, this, _1, _2),
                         bind(&LogicConsumer::onNackForHello, this, _1, _2),
                         bind(&LogicConsumer::onHelloTimeout, this, _1));
}

void
LogicConsumer::onHelloData(const ndn::Interest& interest, const ndn::Data& data)
{
  ndn::Name helloDataName = data.getName();

  _LOG_DEBUG("On Hello Data " << helloDataName);

  m_iblt = helloDataName.getSubName(helloDataName.size()-2, 2);
  _LOG_DEBUG("m_iblt: " << m_iblt);
  std::string content(reinterpret_cast<const char*>(data.getContent().value()),
                        data.getContent().value_size());

  m_helloSent = true;

  m_onRecieveHelloData(content);
}

void
LogicConsumer::sendSyncInterest()
{
  // Sync interest format for partial: /<sync-prefix>/sync/<BF>/<old-IBF>
  // Sync interest format for full: /<sync-prefix>/sync/full/<old-IBF>?

  // name last component is the IBF and content should be the prefix with the version numbers
  assert(m_helloSent);
  assert(!m_iblt.empty());

  ndn::Name syncInterestName = m_syncPrefix;
  syncInterestName.append("sync");

  // Append subscription list
  appendBF(syncInterestName);

  // Append IBF received in hello/sync data
  syncInterestName.append(m_iblt);

  ndn::Interest syncInterest(syncInterestName);
  // Need 4000 in constant (and configurable by the user?)
  syncInterest.setInterestLifetime(ndn::time::milliseconds(4000));
  syncInterest.setMustBeFresh(true);

  _LOG_DEBUG("sendSyncInterest lifetime: " << syncInterest.getInterestLifetime()
              << " nonce=" << syncInterest.getNonce());

  // Remove last pending interest before sending a new one
  if (m_outstandingInterestId != 0) {
      m_face.removePendingInterest(m_outstandingInterestId);
      m_outstandingInterestId = 0;
  }

  m_outstandingInterestId = m_face.expressInterest(syncInterest,
                            bind(&LogicConsumer::onSyncData, this, _1, _2),
                            bind(&LogicConsumer::onNackForSync, this, _1, _2),
                            bind(&LogicConsumer::onSyncTimeout, this, _1));
}

void
LogicConsumer::onSyncData(const ndn::Interest& interest, const ndn::Data& data)
{
  // Need to take care of application nack here
  ndn::Name syncDataName = data.getName();

  _LOG_DEBUG("On Sync Data ");

  m_iblt = syncDataName.getSubName(syncDataName.size()-2, 2);

  std::string content(reinterpret_cast<const char*>(data.getContent().value()),
                        data.getContent().value_size());

  std::stringstream ss(content);
  std::string prefix;
  uint32_t seq;
  std::vector <MissingDataInfo> updates;

  while (ss >> prefix >> seq) {
    //_LOG_INFO("prefix: " << prefix << " m_prefixes[prefix]: " << m_prefixes[prefix] << " seq: " << seq);
    if (m_prefixes.find(prefix) == m_prefixes.end() || seq > m_prefixes[prefix]) {
      // If this is just the next seq number then we had already informed the consumer about
      // the previous sequence number and hence seq low and seq high should be equal to current seq
      updates.push_back(MissingDataInfo(prefix, m_prefixes[prefix]+1, seq));
      m_prefixes[prefix] = seq;
    }
  }

  std::string c = content;
  boost::replace_all(c, "\n", ",");
  _LOG_DEBUG("Sync Data:  " << c);

  if (!updates.empty()) {
    m_onUpdate(updates);
  }

  // If we send a sync interest immediately then it will hit the old
  // pit entry at the repo's NFD (the pit entry is not gone yet because of the straggler timer,
  // which will be eliminated in the future). This will trigger best route strategy to send this interest
  // else where and bring back data from a repo whose IBF is not the same as the first repo's IBF.
  // Thus resulting in a loop of sync data being satisfied from both of the repos everytime
  ndn::time::milliseconds after(m_rangeUniformRandom());
  m_scheduler.scheduleEvent(after, std::bind(&LogicConsumer::sendSyncInterest, this));
}

void
LogicConsumer::onHelloTimeout(const ndn::Interest& interest)
{
  _LOG_DEBUG("on hello timeout");
  this->sendHelloInterest();
}

void
LogicConsumer::onNackForHello(const ndn::Interest& interest, const ndn::lp::Nack& nack)
{
  _LOG_DEBUG("received Nack with reason " << nack.getReason()
             << " for interest " << interest << std::endl);
  // Re-send after a while
  ndn::time::milliseconds after(m_rangeUniformRandom());
  m_scheduler.scheduleEvent(after, std::bind(&LogicConsumer::sendHelloInterest, this));
}

void
LogicConsumer::onNackForSync(const ndn::Interest& interest, const ndn::lp::Nack& nack)
{
  _LOG_DEBUG("received Nack with reason " << nack.getReason()
             << " for interest " << interest << std::endl);
  // Re-send after a while
  ndn::time::milliseconds after(m_rangeUniformRandom());
  m_scheduler.scheduleEvent(after, std::bind(&LogicConsumer::sendHelloInterest, this));
}

void
LogicConsumer::onSyncTimeout(const ndn::Interest& interest)
{
  _LOG_DEBUG("on sync timeout " << interest.getNonce());

  ndn::time::milliseconds after(m_rangeUniformRandom());
  m_scheduler.scheduleEvent(after, std::bind(&LogicConsumer::sendSyncInterest, this));
}

void
LogicConsumer::fetchData(const ndn::Name& sessionName, const uint32_t& seq, int nRetries,
                         const FetchDataCallBack& fdCallback)
{
  ndn::Name interestName;
  interestName.append(sessionName).appendNumber(seq);

  ndn::Interest interest(interestName);
  interest.setMustBeFresh(true);
  interest.setInterestLifetime(ndn::time::milliseconds(4000));

  _LOG_DEBUG("Sending interest: " << interestName << " Nonce: " << interest.getNonce());

  m_face.expressInterest(interest,
                         bind(&LogicConsumer::onData, this, _1, _2, fdCallback),
                         bind(&LogicConsumer::onDataNack, this, _1, _2, nRetries, fdCallback),
                         bind(&LogicConsumer::onDataTimeout, this, _1, nRetries, fdCallback));
}

bool
LogicConsumer::haveSentHello()
{
  return m_helloSent;
}

std::set <std::string>
LogicConsumer::getSL()
{
  return m_sl;
}

void
LogicConsumer::addSL(std::string s)
{
  m_prefixes[s] = 0;
  m_sl.insert(s);
  m_bf.insert(s);
}

std::vector <std::string>
LogicConsumer::getNS()
{
  return m_ns;
}

void
LogicConsumer::appendBF(ndn::Name& name)
{
  name.appendNumber(m_count);
  name.appendNumber((int)(m_false_positive*1000));
  name.appendNumber(m_bf.getTableSize());
  name.append(m_bf.begin(), m_bf.end());
}

void
LogicConsumer::onData(const ndn::Interest& interest, const ndn::Data& data, const FetchDataCallBack& fdCallback)
{
  ndn::Name dataName = data.getName();
  _LOG_INFO("On Data " << dataName.getSubName(1, dataName.size()-2) << "/"
             << dataName.get(dataName.size()-1).toNumber());
  fdCallback(data);
}

void
LogicConsumer::onDataTimeout(const ndn::Interest interest, int nRetries, const FetchDataCallBack& fdCallback)
{
  if (nRetries <= 0) {
    return;
  }

  _LOG_INFO("Data timeout for " << interest);
  ndn::Interest newNonceInterest = interest;
  newNonceInterest.refreshNonce();
  m_face.expressInterest(newNonceInterest,
                         bind(&LogicConsumer::onData, this, _1, _2, fdCallback),
                         bind(&LogicConsumer::onDataNack, this, _1, _2, nRetries - 1, fdCallback),
                         bind(&LogicConsumer::onDataTimeout, this, _1, nRetries - 1, fdCallback));
}

void
LogicConsumer::onDataNack(const ndn::Interest& interest, const ndn::lp::Nack& nack, int nRetries,
                          const FetchDataCallBack& fdCallback)
{
  _LOG_INFO("received Nack with reason " << nack.getReason()
             << " for interest " << interest << std::endl);
  // Re-send after a while
  ndn::time::steady_clock::Duration after = ndn::time::milliseconds(50);
  m_scheduler.scheduleEvent(after, std::bind(&LogicConsumer::onDataTimeout, this, interest,
                                             nRetries - 1, fdCallback));
}

void
LogicConsumer::printSL()
{
  std::string sl = "";
  for (const std::string& s: m_sl) {
    sl += s + " ";
  }
  _LOG_INFO("Subscription List: " << sl);
}

} // namespace psync
