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

#ifndef PSYNC_LOGIC_CONSUMER_HPP
#define PSYNC_LOGIC_CONSUMER_HPP

#include "bloom-filter.hpp"
#include "util.hpp"

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/util/scheduler.hpp>

#include <boost/random.hpp>
#include <utility>
#include <map>
#include <vector>
#include <functional>

namespace psync {

typedef std::function<void(const std::vector<MissingDataInfo>)> UpdateCallback;
typedef std::function<void(const std::string)> RecieveHelloCallback;
typedef std::function<void(const ndn::Data& data)> FetchDataCallBack;

class LogicConsumer
{
public:
  //false_positive: false_positive probability
  LogicConsumer(ndn::Name& prefix,
                ndn::Face& face,
                RecieveHelloCallback& onRecieveHelloData,
                UpdateCallback& onUpdate,
                unsigned int count,
                double false_postive);

  ~LogicConsumer();

  void stop();

  void sendHelloInterest();
  void sendSyncInterest();
  void fetchData(const ndn::Name& sessionName, const uint32_t& seq, int nRetries,
                 const FetchDataCallBack& fdCallback);

  bool haveSentHello();
  std::set <std::string> getSL();
  void addSL(std::string s);
  std::vector <std::string> getNS();
  bool isSub(std::string prefix) {
    return m_suball || m_sl.find(prefix) != m_sl.end();
  }

  void setSeq(std::string prefix, const uint32_t& seq) {
    m_prefixes[prefix] = seq;
  }

  uint32_t getSeq(std::string prefix) {
    return m_prefixes[prefix];
  }

  void printSL();

private:
  void onHelloData(const ndn::Interest& interest, const ndn::Data& data);
  void onSyncData(const ndn::Interest& interest, const ndn::Data& data);
  void onHelloTimeout(const ndn::Interest& interest);
  void onSyncTimeout(const ndn::Interest& interest);
  void onData(const ndn::Interest& interest, const ndn::Data& data, const FetchDataCallBack& fdCallback);
  void onDataTimeout(const ndn::Interest interest, int nRetries, const FetchDataCallBack& fdCallback);
  void onDataNack(const ndn::Interest& interest, const ndn::lp::Nack& nack, int nRetries,
                  const FetchDataCallBack& fdCallback);
  void appendBF(ndn::Name& name);
  void onNackForHello(const ndn::Interest& interest, const ndn::lp::Nack& nack);
  void onNackForSync(const ndn::Interest& interest, const ndn::lp::Nack& nack);

private:
  ndn::Name m_syncPrefix;
  ndn::Face& m_face;
  RecieveHelloCallback m_onRecieveHelloData;
  UpdateCallback m_onUpdate;
  unsigned int m_count;
  double m_false_positive;
  bool m_suball;
  ndn::Name m_iblt;
  std::map <std::string, uint32_t> m_prefixes;
  bool m_helloSent;
  std::set <std::string> m_sl;
  std::vector <std::string> m_ns;
  bloom_filter m_bf;
  const ndn::PendingInterestId* m_outstandingInterestId;
  ndn::Scheduler m_scheduler;

  boost::mt19937 m_randomGenerator;
  boost::variate_generator<boost::mt19937&, boost::uniform_int<> > m_rangeUniformRandom;
};

} // namespace psync

#endif // PSYNC_LOGIC_CONSUMER_HPP