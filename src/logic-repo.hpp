#ifndef LOGIC_REPO_HPP
#define LOGIC_REPO_HPP

#include <map>
#include <unordered_set>

#include <ndn-cxx/common.hpp>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/ims/in-memory-storage-persistent.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/util/time.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include "iblt.hpp"
#include "bloom-filter.hpp"

namespace psync {

  struct PendingEntryInfo {
    PendingEntryInfo(const bloom_filter& bf, const IBLT& iblt)
      : bf(bf)
      , iblt(iblt)
      , expirationEvent(0)
    {}

    /*PendingEntryInfo()
  : bf()
  , iblt(80)
  {}*/

    bloom_filter bf;
    IBLT iblt;
    ndn::EventId expirationEvent;
  };

  class LogicRepo {
  public:
    LogicRepo(size_t expectedNumEntries,
	      ndn::Face& face,
	      ndn::Name& prefix,
	      ndn::time::milliseconds helloReplyFreshness,
	      ndn::time::milliseconds syncReplyFreshness);

    ~LogicRepo();

      void
      addSyncNode(std::string prefix);

      void
      removeSyncNode(std::string prefix);

      void
      publishData(const ndn::Block& content, const ndn::time::milliseconds& freshness,
		  std::string prefix);

      void
      updateSeq(std::string prefix, uint32_t seq);

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

      void
      insertToIMS(const std::shared_ptr<ndn::Data>& data) {
	m_ims.insert(*data);
      }

  private:
      void
      satisfyPendingSyncInterests(const std::string& prefix);

      void
      onInterest(const ndn::Name& prefix, const ndn::Interest& interest);

      void
      onHelloInterest(const ndn::Name& prefix, const ndn::Interest& interest);

      void
      onSyncInterest(const ndn::Name& prefix, const ndn::Interest& interest);

      void
      onSyncRegisterFailed(const ndn::Name& prefix, const std::string& msg);

      void
      sendFragmentedData(const ndn::Name& segmentPrefix, const std::string& content);

  public:
      void
      appendIBLT(ndn::Name& name);

  private:
      void
      sendNack(const ndn::Interest interest);

    std::size_t
    getSize(uint64_t varNumber);

      void
      printEntries(IBLT &iblt, std::string ibltname);

  private:
    IBLT m_iblt;
    uint32_t m_expectedNumEntries;
    uint32_t m_threshold;

    std::map <std::string, uint32_t> m_prefixes; // prefix and sequence number
    std::map <std::string, uint32_t> m_prefix2hash;
    std::map <uint32_t, std::string> m_hash2prefix;
    //std::map <ndn::Name, PendingEntryInfo> m_pendingEntries;
    std::map <ndn::Name, std::shared_ptr<PendingEntryInfo>> m_pendingEntries;

    ndn::Face& m_face;
    ndn::Name m_syncPrefix;
    ndn::KeyChain m_keyChain;

    ndn::Scheduler m_scheduler;

    ndn::time::milliseconds m_helloReplyFreshness;
    ndn::time::milliseconds m_syncReplyFreshness;

    ndn::InMemoryStoragePersistent m_ims;
    ndn::EventId updateSeqScheduled;
  };

}

#endif
