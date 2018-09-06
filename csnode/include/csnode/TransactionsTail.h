#ifndef TRANSACTIONS_TAIL_H
#define TRANSACTIONS_TAIL_H

#include "BitHeap.hpp"

namespace Credits
{
    class TransactionsTail
    {
    public:
        static constexpr size_t BitSize = 32;
        using TransactionId = int64_t;
    public:
        void push(TransactionId trxId)
        {
            heap_.push(trxId);
        }

        bool isAllowed(TransactionId trxId) const
        {
            if (heap_.empty())
                return true;
            else
            {
                const Heap::MinMaxRange& range = heap_.minMaxRange();
                if (trxId > range.second)
                    return true;
                else if (trxId < range.first)
                    return false;
                else
                    return !heap_.contains(trxId);
            }
        }

    private:
        using Heap = BitHeap<TransactionId, BitSize>;
        Heap heap_;
    };

}

#endif
