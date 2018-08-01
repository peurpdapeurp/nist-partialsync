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

//#include <PSync/logic-full.hpp>
//#include <PSync/logging.hpp>

#include "logic-full.hpp"
#include "logging.hpp"

#include <iostream>
#include <random>

using namespace psync;
using namespace ndn;

_LOG_INIT(App);

class Producer
{
public:
  Producer(const Name& syncPrefix, const Name& userPrefix, int maxNumPublish)
    : m_scheduler(m_face.getIoService())
    , m_syncPrefix(syncPrefix)
    , m_userPrefix(userPrefix)
    , m_numPublish(0)
    , m_maxNumPublish(maxNumPublish)
    , m_rng(std::random_device{}())
    , m_rangeUniformRandom(500, 2000)
  {
  }

  void run()
  {
    initializeSyncRepo();

    m_face.processEvents();
  }

protected:
  void initializeSyncRepo()
  {
    m_logic = std::make_shared<LogicFull>(80,
                                          m_face,
                                          m_syncPrefix,
                                          m_userPrefix,
                                          std::bind(&Producer::processSyncUpdate, this, _1),
                                          ndn::time::milliseconds(1000),
                                          ndn::time::milliseconds(1000));

    m_scheduler.scheduleEvent(time::milliseconds(m_rangeUniformRandom(m_rng)),
                              std::bind(&Producer::doUpdate, this));
  }

  void
  doUpdate() {
    if (m_numPublish++ >= m_maxNumPublish) {
      _LOG_DEBUG("Done publishing, no more updates.");
      return;
    }

    m_logic->publishName(m_userPrefix.toUri());
    _LOG_INFO("Publishing data: " << m_userPrefix << "/" << m_logic->getSeq(m_userPrefix.toUri()));

    int interval = m_rangeUniformRandom(m_rng);
    _LOG_DEBUG("Next publish in: " << interval << " ms");
    m_scheduler.scheduleEvent(time::milliseconds(interval), bind(&Producer::doUpdate, this));
  }

  void
  processSyncUpdate(const std::vector<MissingDataInfo>& updates)
  {
    for (const auto& ms : updates) {
      std::string prefix = ms.prefix;
      int seq1 = ms.seq1;
      int seq2 = ms.seq2;
      if (prefix == m_userPrefix.toUri()) {
        _LOG_INFO("Got update for own data " << prefix);
        continue;
      }

      for (int i = seq1; i <= seq2; i++) {
        _LOG_INFO("Update " << prefix << "/" << i);
      }
    }
  }

protected:
  Face m_face;

  Scheduler m_scheduler;

  Name m_syncPrefix;
  Name m_userPrefix;

  std::shared_ptr<LogicFull> m_logic;

  int m_numPublish;
  int m_maxNumPublish;

  std::mt19937 m_rng;
  std::uniform_int_distribution<> m_rangeUniformRandom;
};

int main(int argc, char* argv[]) {
  if ( argc != 4 ) {
    std::cout << "usage: " << argv[0] << " <syncPrefix> <userprefix> <nMaxNumPublish>\n";
  }
  else {
    try {
      Producer producer(argv[1], argv[2], atoi(argv[3]));
      producer.run();
    } catch (const std::exception& e) {
      std::cerr << e.what() << std::endl;
    }
  }
}
