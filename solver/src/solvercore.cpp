#include <callsqueuescheduler.hpp>
#include <consensus.hpp>
#include <solvercontext.hpp>
#include <solvercore.hpp>
#include <stage.hpp>
#include <states/nostate.hpp>

#pragma warning(push)
#pragma warning(disable : 4267 4244 4100 4245)
#include <csnode/node.hpp>
#pragma warning(pop)

#include <csnode/spammer.hpp>
#include <csnode/walletsstate.hpp>

#pragma warning(push)
#pragma warning(disable : 4324)
#include <sodium.h>
#pragma warning(pop)

#include <lib/system/logger.hpp>

#include <functional>
#include <limits>
#include <sstream>
#include <string>

namespace cs
{

  // initial values for SolverCore options

  // To track timeout for active state
  constexpr const bool TimeoutsEnabled = false;
  // To enable make a transition to the same state
  constexpr const bool RepeatStateEnabled = true;
  // Special mode: uses debug transition table
  constexpr const bool DebugModeOn = false;

  // default (test intended) constructor
  SolverCore::SolverCore()
    // options
    : opt_timeouts_enabled(TimeoutsEnabled)
    , opt_repeat_state_enabled(RepeatStateEnabled)
    , opt_debug_mode(DebugModeOn)
    // inner data
    , pcontext(std::make_unique<SolverContext>(*this))
    , tag_state_expired(CallsQueueScheduler::no_tag)
    , req_stop(true)
    , cnt_trusted_desired(Consensus::MinTrustedNodes)
    , total_recv_trans(0)
    , total_accepted_trans(0)
    , cnt_deferred_trans(0)
    , total_duration_ms(0)
    // consensus data
    , cur_round(0)
    // previous solver version instance
    , pnode(nullptr)
    , pws(nullptr)
  {
    if(!opt_debug_mode) {
      if(Consensus::Log) {
        LOG_NOTICE("SolverCore: use default transition table");
      }
      InitTransitions();
    }
    else {
      if(Consensus::Log) {
        LOG_WARN("SolverCore: opt_debug_mode is on, so use special transition table");
      }
      InitDebugModeTransitions();
    }
  }

  // actual constructor
  SolverCore::SolverCore(Node* pNode, csdb::Address GenesisAddress, csdb::Address StartAddress)
    : SolverCore()
  {
    addr_genesis = GenesisAddress;
    addr_start = StartAddress;
    pnode = pNode;
    pws = std::make_unique<cs::WalletsState>(pNode->getBlockChain());
  }

  SolverCore::~SolverCore()
  {
    scheduler.Stop();
    transitions.clear();
  }

  void SolverCore::ExecuteStart(Event start_event)
  {
    if(!is_finished()) {
      cswarning() << "SolverCore: cannot start again, already started";
      return;
    }
    req_stop = false;
    handleTransitions(start_event);
  }

  void SolverCore::finish()
  {
    if(pstate) {
      pstate->off(*pcontext);
    }
    scheduler.RemoveAll();
    tag_state_expired = CallsQueueScheduler::no_tag;
    pstate = std::make_shared<NoState>();
    req_stop = true;
  }

  void SolverCore::setState(const StatePtr& pState)
  {
    if(!opt_repeat_state_enabled) {
      if(pState == pstate) {
        return;
      }
    }
    if(tag_state_expired != CallsQueueScheduler::no_tag) {
      // no timeout, cancel waiting
      scheduler.Remove(tag_state_expired);
      tag_state_expired = CallsQueueScheduler::no_tag;
    }
    else {
      // state changed due timeout from within expired state
    }

    if(pstate) {
      pstate->off(*pcontext);
    }
    if(Consensus::Log) {
      cslog() << "SolverCore: switch " << (pstate ? pstate->name() : "null") << " -> "
        << (pState ? pState->name() : "null");
    }
    pstate = pState;
    if(!pstate) {
      return;
    }
    pstate->on(*pcontext);

    // timeout handling
    if(opt_timeouts_enabled) {
      tag_state_expired =
        scheduler.InsertOnce(Consensus::DefaultStateTimeout,
          [this]() {
        cslog() << "SolverCore: state " << pstate->name() << " is expired";
        // clear flag to know timeout expired
        tag_state_expired = CallsQueueScheduler::no_tag;
        // control state switch
        std::weak_ptr<INodeState> p1(pstate);
        pstate->expired(*pcontext);
        if(pstate == p1.lock()) {
          // expired state did not change to another one, do it now
          cslog() << "SolverCore: there is no state set on expiration of " << pstate->name();
          // setNormalState();
        }
      },
          true /*replace if exists*/);
    }
  }

  void SolverCore::handleTransitions(Event evt)
  {
    if(!pstate) {
      // unable to work until initTransitions() called
      return;
    }
    if(Event::BigBang == evt) {
      cswarning() << "SolverCore: BigBang on";
    }
    const auto& variants = transitions[pstate];
    if(variants.empty()) {
      cserror() << "SolverCore: there are no transitions for " << pstate->name();
      return;
    }
    auto it = variants.find(evt);
    if(it == variants.cend()) {
      // such event is ignored in current state
      csdebug() << "SolverCore: event " << static_cast<int>(evt) << " ignored in state " << pstate->name();
      return;
    }
    setState(it->second);
  }

  bool SolverCore::stateCompleted(Result res)
  {
    if(Result::Failure == res) {
      cserror() << "SolverCore: error in state " << (pstate ? pstate->name() : "null");
    }
    return (Result::Finish == res);
  }

  void SolverCore::spawn_next_round(const std::vector<cs::PublicKey>& trusted_nodes)
  {
    cslog() << "SolverCore: TRUSTED -> WRITER, do write & send block";

    cs::RoundTable table;
    table.round = cs::Conveyer::instance().currentRoundNumber() + 1;
    table.confidants = trusted_nodes;
    pnode->prepareMetaForSending(table);
  }

  void SolverCore::store_received_block(csdb::Pool& p, bool /*defer_write*/)
  {
    cslog() << "SolverCore: store received block #" << p.sequence()
      << ", " << p.transactions_count() << " transactions";

    // see: Solver-1, method Solver::gotBlock()
    if(!pnode->getBlockChain().storeBlock(p)) {
      cserror() << "SolverCore: block sync required";
      return;
    }

    total_accepted_trans += p.transactions_count();
  }

  bool SolverCore::is_block_deferred() const
  {
    return false;  // pnode->getBlockChain().isLastBlockDeferred();
  }

  void SolverCore::flush_deferred_block()
  {
    // if nothing to save deferred_block has zero sequence number
    if(!is_block_deferred()) {
      return;
    }
    // pnode->getBlockChain().writeDeferredBlock();
    total_accepted_trans += cnt_deferred_trans;
    cnt_deferred_trans = 0;
  }

  void SolverCore::drop_deferred_block()
  {
    if(!is_block_deferred()) {
      return;
    }
    if(false /*pnode->getBlockChain().revertLastBlock()*/) {
      // TODO: bc.revertWalletsInPool(deferred_block);
      cswarning() << "SolverCore: deferred block dropped, wallets are reverted";
    }
    else {
      cserror() << "SolverCore: cannot drop deferred block";
      total_accepted_trans += cnt_deferred_trans;
    }
    cnt_deferred_trans = 0;
  }

}  // namespace slv2
