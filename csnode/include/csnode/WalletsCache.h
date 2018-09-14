#ifndef WALLETS_CACHE_H
#define WALLETS_CACHE_H

#include <memory>
#include <vector>
#include <boost/dynamic_bitset.hpp>
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <csnode/TransactionsTail.h>

namespace csdb
{
    class Pool;
    class Transaction;
}

namespace Credits
{
    class WalletsIds;

    class WalletsCache
    {
    public:
        using WalletId = csdb::internal::WalletId;
        using Mask = boost::dynamic_bitset<uint64_t>;

        struct Config
        {
            size_t initialWalletsNum_ = 2 * 1024 * 1024;
        };

    public:
        struct WalletData
        {
            using Address = std::array<uint8_t, 32>;

            Address address_;
            csdb::Amount balance_;
            TransactionsTail trxTail_;
        };

    public:
        static void convert(const csdb::Address& address, WalletData::Address& walletAddress);
        static void convert(const WalletData::Address& walletAddress, csdb::Address& address);
    private:
        using Data = std::vector<WalletData*>;

        class ProcessorBase
        {
        public:
            ProcessorBase(WalletsCache& data) : data_(data)
            {}
            virtual ~ProcessorBase()
            {}
            virtual bool findWalletId(const csdb::Address& address, WalletId& id) = 0;

        protected:
            void load(csdb::Pool& curr);
            void load(const csdb::Transaction& tr);
            void loadTrxForSource(const csdb::Transaction& tr);
            void loadTrxForTarget(const csdb::Transaction& tr);
            virtual WalletData& getWalletData(WalletId id, const csdb::Address& address) = 0;
            virtual void setModified(WalletId id) = 0;
        protected:
            static WalletData& getWalletData(Data& wallets, WalletId id, const csdb::Address& address);
        protected:
            WalletsCache& data_;
        };
    public:
        class Initer : protected ProcessorBase
        {
        public:
            Initer(WalletsCache& data);
            void loadPrevBlock(csdb::Pool& curr);
            bool moveData(WalletId srcIdSpecial, WalletId destIdNormal);
        protected:
            bool findWalletId(const csdb::Address& address, WalletId& id) override;
            WalletData& getWalletData(WalletId id, const csdb::Address& address) override;
            void setModified(WalletId id) override;
        protected:
            Data walletsSpecial_;
        };

        class Updater : protected ProcessorBase
        {
        public:
            Updater(WalletsCache& data) : ProcessorBase(data)
            {}

            void loadNextBlock(csdb::Pool& curr);
            const WalletData* findWallet(WalletId id) const;
            const Mask& getModified() const { return modified_; }
        protected:
            bool findWalletId(const csdb::Address& address, WalletId& id) override;
            WalletData& getWalletData(WalletId id, const csdb::Address& address) override;
            void setModified(WalletId id) override;
        protected:
            Mask modified_;
        };

    public:
        WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, WalletsIds& walletsIds);
        ~WalletsCache();
        WalletsCache(const WalletsCache&) = delete;
        WalletsCache& operator=(const WalletsCache&) = delete;
        WalletsCache(const WalletsCache&&) = delete;
        WalletsCache& operator=(const WalletsCache&&) = delete;

        std::unique_ptr<Initer> createIniter();
        std::unique_ptr<Updater> createUpdater();

    private:
        const Config config_;
        WalletsIds& walletsIds_;
        const csdb::Address genesisAddress_;
        const csdb::Address startAddress_;

        Data wallets_;
    };

} // namespace Credits

#endif
