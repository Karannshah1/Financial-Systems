#include <iostream>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <random>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <algorithm>
#include <memory>

using namespace std;

class FinancialTransactionSystem {
public:
    class Transaction;

private:
    struct TransactionInfo {
        function<void(Transaction&)> logic;
        int priority;
        string description;
        chrono::steady_clock::time_point startTime;

        TransactionInfo(function<void(Transaction&)> l, int p, string desc)
            : logic(move(l)), priority(p), description(move(desc)), startTime(chrono::steady_clock::now()) {}
    };

    struct CompareTransactionInfo {
        bool operator()(const TransactionInfo& lhs, const TransactionInfo& rhs) {
            if (lhs.priority != rhs.priority) {
                return lhs.priority < rhs.priority;
            }
            return lhs.startTime > rhs.startTime;
        }
    };

    map<unsigned, vector<pair<unsigned, double>>> versionedData;
    mutex globalLock;
    mt19937 rng;

    vector<thread> workerThreads;
    priority_queue<TransactionInfo, vector<TransactionInfo>, CompareTransactionInfo> transactionQueue;
    mutex queueMutex;
    condition_variable queueCV;
    atomic<bool> shutdownFlag{false};
    atomic<unsigned> globalClock{0};
    atomic<int> activeTransactions{0};

public:
    FinancialTransactionSystem(unsigned numThreads = thread::hardware_concurrency())
        : rng(random_device{}()) {
        for (unsigned i = 0; i < numThreads; ++i) {
            workerThreads.emplace_back(&FinancialTransactionSystem::workerFunction, this);
        }
    }

    ~FinancialTransactionSystem() {
        shutdownFlag.store(true);
        queueCV.notify_all();
        for (auto& thread : workerThreads) {
            thread.join();
        }
    }

    void createAccount(unsigned accountId, double initialBalance) {
        lock_guard<mutex> guard(globalLock);
        versionedData[accountId].emplace_back(0, initialBalance);
    }

    class Transaction {
    private:
        FinancialTransactionSystem& parentSystem;
        map<unsigned, pair<double, unsigned>> readSet;
        map<unsigned, double> writeSet;
        unsigned startTimestamp;
        unsigned endTimestamp;

    public:
        Transaction(FinancialTransactionSystem& system)
            : parentSystem(system), startTimestamp(system.globalClock.load()) {}

        double readBalance(unsigned accountId) {
            if (writeSet.find(accountId) != writeSet.end()) {
                return writeSet[accountId];
            }

            lock_guard<mutex> guard(parentSystem.globalLock);
            auto it = parentSystem.versionedData.find(accountId);
            if (it == parentSystem.versionedData.end()) {
                throw out_of_range("Account not found");
            }

            const auto& versions = it->second;
            auto versionIt = lower_bound(versions.rbegin(), versions.rend(), 
                                         make_pair(startTimestamp, 0.0),
                                         [](const auto& a, const auto& b) { return a.first > b.first; });

            if (versionIt == versions.rend()) {
                throw runtime_error("No valid version found for account " + to_string(accountId));
            }
            readSet[accountId] = make_pair(versionIt->second, versionIt->first);
            return versionIt->second;
        }

        void updateBalance(unsigned accountId, double newBalance) {
            writeSet[accountId] = newBalance;
        }

        bool commit() {
            lock_guard<mutex> guard(parentSystem.globalLock);
            
            endTimestamp = ++parentSystem.globalClock;
            
            for (const auto& entry : readSet) {
                unsigned accountId = entry.first;
                unsigned readVersion = entry.second.second;
                const auto& versions = parentSystem.versionedData[accountId];
                auto it = lower_bound(versions.begin(), versions.end(), 
                                      make_pair(endTimestamp, 0.0),
                                      [](const auto& a, const auto& b) { return a.first < b.first; });
                if (it != versions.begin()) {
                    --it;
                    if (it->first > readVersion) {
                        return false;  // Conflict detected
                    }
                }
            }

            for (const auto& entry : writeSet) {
                unsigned accountId = entry.first;
                double newBalance = entry.second;
                parentSystem.versionedData[accountId].emplace_back(endTimestamp, newBalance);
            }

            return true;
        }
    };

    void scheduleTransaction(const function<void(Transaction&)>& transactionLogic, int priority, const string& description) {
        lock_guard<mutex> lock(queueMutex);
        transactionQueue.emplace(transactionLogic, priority, description);
        activeTransactions++;
        queueCV.notify_one();
    }

    void executeTrade(unsigned buyerAccountId, unsigned sellerAccountId, double amount) {
        scheduleTransaction([buyerAccountId, sellerAccountId, amount](Transaction& tx) {
            double buyerBalance = tx.readBalance(buyerAccountId);
            double sellerBalance = tx.readBalance(sellerAccountId);

            if (buyerBalance >= amount) {
                tx.updateBalance(buyerAccountId, buyerBalance - amount);
                tx.updateBalance(sellerAccountId, sellerBalance + amount);
            } else {
                throw runtime_error("Insufficient funds for trade");
            }
        }, 10, "Stock trade");
    }

    void transferFunds(unsigned fromAccountId, unsigned toAccountId, double amount) {
        scheduleTransaction([fromAccountId, toAccountId, amount](Transaction& tx) {
            double fromBalance = tx.readBalance(fromAccountId);
            double toBalance = tx.readBalance(toAccountId);

            if (fromBalance >= amount) {
                tx.updateBalance(fromAccountId, fromBalance - amount);
                tx.updateBalance(toAccountId, toBalance + amount);
            } else {
                throw runtime_error("Insufficient funds for transfer");
            }
        }, 5, "Bank transfer");
    }

    void executeCryptoTrade(unsigned buyerAccountId, unsigned sellerAccountId, double cryptoAmount, double fiatAmount) {
        scheduleTransaction([buyerAccountId, sellerAccountId, cryptoAmount, fiatAmount](Transaction& tx) {
            double buyerFiatBalance = tx.readBalance(buyerAccountId);
            double sellerCryptoBalance = tx.readBalance(sellerAccountId);

            if (buyerFiatBalance >= fiatAmount && sellerCryptoBalance >= cryptoAmount) {
                tx.updateBalance(buyerAccountId, buyerFiatBalance - fiatAmount);
                tx.updateBalance(sellerAccountId, sellerCryptoBalance - cryptoAmount);
                
                unsigned buyerCryptoWalletId = buyerAccountId + 1000000;
                unsigned sellerFiatWalletId = sellerAccountId + 2000000;
                
                double buyerCryptoBalance = tx.readBalance(buyerCryptoWalletId);
                double sellerFiatBalance = tx.readBalance(sellerFiatWalletId);
                
                tx.updateBalance(buyerCryptoWalletId, buyerCryptoBalance + cryptoAmount);
                tx.updateBalance(sellerFiatWalletId, sellerFiatBalance + fiatAmount);
            } else {
                throw runtime_error("Insufficient funds for crypto trade");
            }
        }, 10, "Crypto trade");
    }

private:
    void workerFunction() {
        while (!shutdownFlag.load()) {
            unique_ptr<TransactionInfo> transactionInfo;
            {
                unique_lock<mutex> lock(queueMutex);
                queueCV.wait(lock, [this] { return !transactionQueue.empty() || shutdownFlag.load(); });
                if (shutdownFlag.load()) return;
                transactionInfo.reset(new TransactionInfo(transactionQueue.top()));
                transactionQueue.pop();
            }

            bool success = false;
            int attempts = 0;
            const int maxAttempts = 10;

            while (!success && attempts < maxAttempts) {
                Transaction tx(*this);
                try {
                    transactionInfo->logic(tx);
                    success = tx.commit();
                } catch (const exception& e) {
                    cout << "Transaction error: " << e.what() << endl;
                    success = false;
                }

                if (!success) {
                    this_thread::sleep_for(chrono::milliseconds(1));
                    attempts++;
                }
            }

            if (success) {
                cout << "Transaction succeeded: " << transactionInfo->description << endl;
            } else {
                cout << "Transaction failed after " << maxAttempts << " attempts: " << transactionInfo->description << endl;
            }
            activeTransactions--;
        }
    }

public:
    void waitForCompletion() {
        while (activeTransactions.load() > 0) {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }

    void printAccountBalance(unsigned accountId) {
        lock_guard<mutex> guard(globalLock);
        auto it = versionedData.find(accountId);
        if (it != versionedData.end() && !it->second.empty()) {
            cout << "Account " << accountId << " balance: " << it->second.back().second << endl;
        } else {
            cout << "Account " << accountId << " not found or empty" << endl;
        }
    }
};

int main() {
    FinancialTransactionSystem fts;

    fts.createAccount(1, 10000);
    fts.createAccount(2, 20000);
    fts.createAccount(3, 30000);
    fts.createAccount(1000001, 100);
    fts.createAccount(2000002, 200);

    cout << "Executing stock trade..." << endl;
    fts.executeTrade(1, 2, 5000);

    cout << "Executing bank transfer..." << endl;
    fts.transferFunds(2, 3, 1000);

    cout << "Executing crypto trade..." << endl;
    fts.executeCryptoTrade(1, 2, 50, 5000);

    fts.waitForCompletion();

    cout << "\nFinal balances:" << endl;
    fts.printAccountBalance(1);
    fts.printAccountBalance(2);
    fts.printAccountBalance(3);
    fts.printAccountBalance(1000001);
    fts.printAccountBalance(2000002);

    return 0;
}