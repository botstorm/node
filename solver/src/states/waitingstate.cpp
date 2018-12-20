#include "waitingstate.hpp"
#include <consensus.hpp>
#include <solvercontext.hpp>
#include <sstream>

namespace cs {

void WaitingState::on(SolverContext &context) {
  // TODO:: calculate my queue number starting from writing node:
  const auto ptr = context.stage3((uint8_t)context.own_conf_number());
  writingQueueNumber_ = ptr->realTrustedMask.at(ptr->sender);  
  //(int)(((uint8_t)ptr->sender + (uint8_t)context.cnt_trusted() - (uint8_t)ptr->writer)) % (int)context.cnt_trusted();
  if (writingQueueNumber_ == InvalidConfidant) {
    return;
  }
  std::ostringstream os;
  os << prefix_ << "-" << (size_t)writingQueueNumber_;
  myName_ = os.str();

  cslog() << name() << ": my order " << (int)ptr->sender << ", trusted amount " << (int)context.cnt_trusted()
          << ", writer " << (int)ptr->writer;
  if (writingQueueNumber_ == 0) {
    cslog() << name() << ": becoming WRITER";
    context.request_role(Role::Writer);
    return;
  }

  // TODO: value = (Consensus::PostConsensusTimeout * "own number in "writing" queue")
  uint32_t value = sendRoundTableDelayMs_ * writingQueueNumber_;

  if (Consensus::Log) {
    cslog() << name() << ": start wait " << value / 1000 << " sec until new round";
  }

  SolverContext *pctx = &context;
  roundTimeout_.start(context.scheduler(), value,
                      [pctx, this]() {
                        if (Consensus::Log) {
                          cslog() << name() << ": time to wait new round is expired";
                        }
                        activate_new_round(*pctx);
                      },
                      true /*replace existing*/);
}

void WaitingState::off(SolverContext & /*context*/) {
  myName_ = prefix_;
  if (roundTimeout_.cancel()) {
    if (Consensus::Log) {
      cslog() << name() << ": cancel wait new round";
    }
  }
}

void WaitingState::activate_new_round(SolverContext &context) {
  cslog() << name() << ": activating new round ";
  const auto ptr = context.stage3((uint8_t)context.own_conf_number());
  if (ptr == nullptr) {
    cserror() << name() << ": cannot access own stage data, didnt you forget to cancel this call?";
    return;
  }

  context.request_round_info(
      (uint8_t)((int)(ptr->writer + writingQueueNumber_ - 1)) % (int)context.cnt_trusted(),   // previous "writer"
      (uint8_t)((int)(ptr->writer + writingQueueNumber_ + 1)) % (int)context.cnt_trusted());  // next "writer"
}

}  // namespace cs
