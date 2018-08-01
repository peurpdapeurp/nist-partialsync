#define BOOST_LOG_DYN_LINK 1

#include <iostream>
#include <cstring>
#include <limits>

#include "logic-repo.hpp"

#include <ndn-cxx/common.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>


#include "logging.hpp"
#include "util.hpp"

namespace pt = boost::posix_time;

namespace psync {

  _LOG_INIT(LogicRepo);

  static const size_t N_HASHCHECK = 11;

  LogicRepo::LogicRepo(size_t expectedNumEntries,
		       ndn::Face& face,
		       ndn::Name& prefix,
		       ndn::time::milliseconds helloReplyFreshness,
		       ndn::time::milliseconds syncReplyFreshness)
    : m_iblt(expectedNumEntries)
    , m_expectedNumEntries(expectedNumEntries)
    , m_threshold(expectedNumEntries/2)
    , m_face(face)
    , m_syncPrefix(prefix)
    , m_scheduler(m_face.getIoService())
    , m_helloReplyFreshness(helloReplyFreshness)
    , m_syncReplyFreshness(syncReplyFreshness)
  {
    ndn::Name helloName = m_syncPrefix;
    helloName.append("hello");
    m_face.setInterestFilter(helloName,
			     bind(&LogicRepo::onHelloInterest, this, _1, _2),
			     bind(&LogicRepo::onSyncRegisterFailed, this, _1, _2));

    ndn::Name syncName = m_syncPrefix;
    syncName.append("sync");
    m_face.setInterestFilter(syncName,
			     bind(&LogicRepo::onSyncInterest, this, _1, _2),
			     bind(&LogicRepo::onSyncRegisterFailed, this, _1, _2));

    m_face.setInterestFilter("BMS",
			     bind(&LogicRepo::onInterest, this, _1, _2),
			     [] (const ndn::Name& prefix, const std::string& msg) {});
  }

  LogicRepo::~LogicRepo()
  {
    m_face.shutdown();
  }

  void
  LogicRepo::addSyncNode(std::string prefix)
  {
    if (m_prefixes.find(prefix) == m_prefixes.end()) {
      m_prefixes[prefix] = 0;
    }

    // add it to the iblt.
    std::string prefixWithSeq = prefix + "/" + std::to_string(m_prefixes[prefix]);
    uint32_t newHash = MurmurHash3(N_HASHCHECK, ParseHex(prefixWithSeq));
    m_prefix2hash[prefixWithSeq] = newHash;
    m_hash2prefix[newHash] = prefix;
    m_iblt.insert(newHash);

    /*m_face.setInterestFilter(prefix,
                           bind(&LogicRepo::onInterest, this, _1, _2),
                           [] (const ndn::Name& prefix, const std::string& msg) {});*/
  }

  void
  LogicRepo::removeSyncNode(std::string prefix)
  {
    // NEED TO UNSET INTEREST FILTER HERE IF SET IN ADD SYNC NODE?
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
  LogicRepo::publishData(const ndn::Block& content, const ndn::time::milliseconds& freshness,
			 std::string prefix)
  {
    if (m_prefixes.find(prefix) == m_prefixes.end()) {
      return;
    }

    ndn::shared_ptr<ndn::Data> data = ndn::make_shared<ndn::Data>();
    data->setContent(content);
    data->setFreshnessPeriod(freshness);

    uint32_t newSeq = m_prefixes[prefix] + 1;
    ndn::Name dataName;
    dataName.append(ndn::Name(prefix).appendNumber(newSeq));
    data->setName(dataName);
    m_keyChain.sign(*data);
    m_ims.insert(*data);

    _LOG_INFO("Publish: "<< prefix << "/" << newSeq);

    try {
      updateSeq(prefix, m_prefixes[prefix]+1);
    } catch (const std::exception& e) {
      _LOG_DEBUG("Error: " << e.what());
      std::cout << "Error: " << e.what() << std::endl;
    }
  }

  void
  LogicRepo::onInterest(const ndn::Name& prefix, const ndn::Interest& interest)
  {
    ndn::Name interestName = interest.getName();
    _LOG_DEBUG("On Interest " << interestName << " Nonce: " << interest.getNonce());
    ndn::Name prefixWithoutPubSubGroup = interestName.getSubName(1, interestName.size()-1);
    //std::cout << "On Interest " << prefix.toUri() << std::endl;
    ndn::shared_ptr<const ndn::Data> data = m_ims.find(ndn::Interest(prefixWithoutPubSubGroup));

    if (static_cast<bool>(data)) {
      _LOG_DEBUG("Found Data, sending back: " << interestName);
      ndn::Data dataWithOldName(*data);
      dataWithOldName.setName(interestName);
      m_face.put(dataWithOldName);
    } else {
      /*ndn::shared_ptr<ndn::Data> dataWithOldName = ndn::make_shared<ndn::Data>();
    dataWithOldName->setName(interestName);
    m_keyChain.sign(*dataWithOldName);
    m_face.put(*dataWithOldName);*/
      _LOG_DEBUG("Data Not Found");
    }
  }

  void
  LogicRepo::onHelloInterest(const ndn::Name& prefix, const ndn::Interest& interest)
  {
    //!! The way our segment publisher/fetcher hello is set means hello can never be answered
    // from the content store

    _LOG_DEBUG("Hello Interest Received " << prefix.toUri() << " Nonce " << interest.getNonce());

    std::string content = "";
    for (auto p : m_prefixes) {
      content += p.first + "\n";
    }
    _LOG_DEBUG("sending content p: " << content);

    ndn::Name segmentPrefix = prefix;
    appendIBLT(segmentPrefix);

    sendFragmentedData(segmentPrefix, content);
  }

  void
  LogicRepo::sendFragmentedData(const ndn::Name& segmentPrefix, const std::string& content)
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
  LogicRepo::onSyncInterest(const ndn::Name& prefix, const ndn::Interest& interest)
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
    bf.setTable(std::vector <uint8_t>(bfName.begin()+this->getSize(bfSize), bfName.end()));

    std::vector <uint8_t> testTable(bfName.begin(), bfName.end());

    std::vector <uint8_t> ibltValues(ibltName.begin()+this->getSize(ibltSize), ibltName.end());
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
    for (auto hash : positive) {
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
      //ndn::shared_ptr<ndn::Data> data = ndn::make_shared<ndn::Data>();
      ndn::Name syncDataName = interest.getName();
      appendIBLT(syncDataName);

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
				[this, interest] () {
				  _LOG_DEBUG("Erase Pending Interest " << interest.getNonce());
				  m_pendingEntries.erase(interest.getName());
				});
  }

  void
  LogicRepo::onSyncRegisterFailed(const ndn::Name& prefix, const std::string& msg)
  {
    _LOG_ERROR("Logic::onSyncRegisterFailed");
  }

  void
  LogicRepo::appendIBLT(ndn::Name& name)
  {
    printEntries(m_iblt, "appending m_iblt");
    std::vector <HashTableEntry> hashTable = m_iblt.getHashTable();
    size_t N = hashTable.size();
    size_t unitSize = 32*3/8; // hard coding
    size_t tableSize = unitSize*N;

    std::vector <uint8_t> table(tableSize);

    for (uint i = 0; i < N; i++) {
      // table[i*12],   table[i*12+1], table[i*12+2], table[i*12+3] --> hashTable[i].count

      table[i*12]   = 0xFF & hashTable[i].count;
      table[i*12+1] = 0xFF & (hashTable[i].count >> 8);
      table[i*12+2] = 0xFF & (hashTable[i].count >> 16);
      table[i*12+3] = 0xFF & (hashTable[i].count >> 24);

      // table[i*12+4], table[i*12+5], table[i*12+6], table[i*12+7] --> hashTable[i].keySum

      table[i*12+4] = 0xFF & hashTable[i].keySum;
      table[i*12+5] = 0xFF & (hashTable[i].keySum >> 8);
      table[i*12+6] = 0xFF & (hashTable[i].keySum >> 16);
      table[i*12+7] = 0xFF & (hashTable[i].keySum >> 24);

      // table[i*12+8], table[i*12+9], table[i*12+10], table[i*12+11] --> hashTable[i].keyCheck

      table[i*12+8] = 0xFF & hashTable[i].keyCheck;
      table[i*12+9] = 0xFF & (hashTable[i].keyCheck >> 8);
      table[i*12+10] = 0xFF & (hashTable[i].keyCheck >> 16);
      table[i*12+11] = 0xFF & (hashTable[i].keyCheck >> 24);
    }

    name.appendNumber(table.size());
    _LOG_DEBUG("Size of IBF hash table: " << hashTable.size());
    _LOG_DEBUG("Size of table appended: " << table.size());
    name.append(table.begin(), table.end());
  }

  void
  LogicRepo::sendNack(const ndn::Interest interest)
  {
    std::string content = "NACK 0";
    ndn::shared_ptr<ndn::Data> data = ndn::make_shared<ndn::Data>();
    data->setName(interest.getName());
    data->setFreshnessPeriod(m_syncReplyFreshness);
    data->setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
    m_keyChain.sign(*data);
    m_face.put(*data);
  }

  std::size_t
  LogicRepo::getSize(uint64_t varNumber)
  {
    std::size_t ans = 1;
    if (varNumber < 253) {
      ans += 1;
    }
    else if (varNumber <= std::numeric_limits<uint16_t>::max()) {
      ans += 3;
    }
    else if (varNumber <= std::numeric_limits<uint32_t>::max()) {
      ans += 5;
    }
    else {
      ans += 9;
    }

    return ans;
  }

  void
  LogicRepo::updateSeq(std::string prefix, uint32_t seq)
  {
    _LOG_DEBUG("UpdateSeq " << prefix << " " << seq);
    if (m_prefixes.find(prefix) != m_prefixes.end() && m_prefixes[prefix] >= seq) {
      return;
    }

    // Delete the last sequence prefix from the iblt
    if (m_prefixes.find(prefix) != m_prefixes.end()) {
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

    satisfyPendingSyncInterests(prefix);
  }

  void
  LogicRepo::satisfyPendingSyncInterests(const std::string& prefix) {
    _LOG_DEBUG("size of pending interest: " << m_pendingEntries.size());
    std::vector <ndn::Name> prefixToErase;

    // Satisfy pending interests
    for (auto pendingInterest : m_pendingEntries) {
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
	} else {
	  _LOG_DEBUG("Sending with empty content so that consumer's IBF is updated since the threshold was crossed");
	}

	// generate sync data and cancel the scheduler
	ndn::Name syncDataName = pendingInterest.first;
	appendIBLT(syncDataName);

	sendFragmentedData(syncDataName, syncContent);

	/*ndn::shared_ptr<ndn::Data> syncData = ndn::make_shared<ndn::Data>();
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

    for (auto pte : prefixToErase) {
      m_pendingEntries.erase(pte);
    }
  }

  void
  LogicRepo::printEntries(IBLT &iblt, std::string ibltname) {
    std::set <uint32_t> t1, t2;
    iblt.listEntries(t1, t2);

    std::string combined = "      ";

    _LOG_DEBUG(ibltname << ":");
    if (t1.size() != 0) { combined += "positive: "; }
    for (auto t:t1) {
      combined += m_hash2prefix[t] + "/" + std::to_string(m_prefixes[m_hash2prefix[t]]) + ", ";
    }
    if (combined != "      ") {
      _LOG_DEBUG(combined);
      combined = "      ";
    }

    if (t2.size() != 0) { combined += "negative: "; }
    for (auto t:t2) {
      combined += m_hash2prefix[t] + "/" + std::to_string(m_prefixes[m_hash2prefix[t]]) + ", ";
    }
    if (combined != "      ") {
      _LOG_DEBUG(combined);
    }
  }

} // namespace psync
