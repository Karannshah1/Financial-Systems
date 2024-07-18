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

class HybridTransactionalMemorySystem {
private:
    struct DataCell {
        int content;
        std::atomic<unsigned> modificationCount;
        std::mutex cellLock;

        DataCell(int initial) : content(initial), modificationCount(0) {}

        DataCell(const DataCell&) = delete;
        DataCell& operator=(const DataCell&) = delete;

        DataCell(DataCell&& other) 
            : content(other.content), 
              modificationCount(other.modificationCount.load()),
              cellLock() {}

        DataCell& operator=(DataCell&& other) {
            if (this != &other) {
                content = other.content;
                modificationCount = other.modificationCount.load();
            }
            return *this;
        }
    };

    std::map<unsigned, DataCell> dataStore;
    std::mutex globalLock;
    std::atomic<bool> useHTM;
    std::mt19937 rng;

public:
    HybridTransactionalMemorySystem() : useHTM(true), rng(std::random_device{}()) {}

    class Transaction {
    private:
        HybridTransactionalMemorySystem& parentSystem;
        std::map<unsigned, unsigned> readLog;
        std::map<unsigned, int> writeBuffer;
        bool isHTM;

    public:
        Transaction(HybridTransactionalMemorySystem& system, bool htm) 
            : parentSystem(system), isHTM(htm) {}

        int fetch(unsigned location) {
            if (writeBuffer.find(location) != writeBuffer.end()) {
                return writeBuffer[location];
            }
            auto it = parentSystem.dataStore.find(location);
            if (it == parentSystem.dataStore.end()) {
                throw std::out_of_range("Memory location not initialized");
            }
            DataCell& cell = it->second;
            int value = cell.content;
            if (!isHTM) {
                unsigned currentMod = cell.modificationCount.load();
                readLog[location] = currentMod;
            }
            return value;
        }

        void store(unsigned location, int value) {
            writeBuffer[location] = value;
        }

        bool finalize() {
            if (isHTM) {
                return finalizeHTM();
            } else {
                return finalizeSTM();
            }
        }

    private:
        bool finalizeSTM() {
            std::lock_guard<std::mutex> systemLock(parentSystem.globalLock);
            
            for (const auto& entry : readLog) {
                unsigned loc = entry.first;
                unsigned modCount = entry.second;
                auto it = parentSystem.dataStore.find(loc);
                if (it == parentSystem.dataStore.end() || it->second.modificationCount.load() != modCount) {
                    return false;  // Transaction conflict detected
                }
            }

            for (const auto& entry : writeBuffer) {
                unsigned loc = entry.first;
                int newValue = entry.second;
                auto it = parentSystem.dataStore.find(loc);
                if (it == parentSystem.dataStore.end()) {
                    parentSystem.dataStore.emplace(std::piecewise_construct,
                                                   std::forward_as_tuple(loc),
                                                   std::forward_as_tuple(newValue));
                } else {
                    DataCell& cell = it->second;
                    std::lock_guard<std::mutex> cellGuard(cell.cellLock);
                    cell.content = newValue;
                    cell.modificationCount++;
                }
            }

            return true;
        }

        bool finalizeHTM() {
            // Simulate HTM commit
            std::uniform_real_distribution<> dis(0.0, 1.0);
            if (dis(parentSystem.rng) < 0.9) {  // 90% success rate for HTM
                for (const auto& entry : writeBuffer) {
                    unsigned loc = entry.first;
                    int newValue = entry.second;
                    auto it = parentSystem.dataStore.find(loc);
                    if (it == parentSystem.dataStore.end()) {
                        parentSystem.dataStore.emplace(std::piecewise_construct,
                                                       std::forward_as_tuple(loc),
                                                       std::forward_as_tuple(newValue));
                    } else {
                        DataCell& cell = it->second;
                        cell.content = newValue;
                        cell.modificationCount++;
                    }
                }
                return true;
            }
            return false;  // HTM transaction failed
        }
    };

    void initializeMemory(unsigned location, int value) {
        std::lock_guard<std::mutex> guard(globalLock);
        dataStore.emplace(std::piecewise_construct,
                          std::forward_as_tuple(location),
                          std::forward_as_tuple(value));
    }

    bool executeTransaction(const std::function<void(Transaction&)>& transactionLogic) {
        bool success = false;
        int attempts = 0;
        const int maxAttempts = 10;  // Increased from 3 to 10

        while (!success && attempts < maxAttempts) {
            bool useHTMForThisAttempt = useHTM.load() && attempts == 0;
            Transaction transaction(*this, useHTMForThisAttempt);
            
            try {
                transactionLogic(transaction);
                success = transaction.finalize();
            } catch (const std::exception&) {
                success = false;
            }

            if (!success) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Add a small delay
                attempts++;
                if (useHTMForThisAttempt) {
                    useHTM.store(false);  // Switch to STM after HTM failure
                }
            }
        }

        return success;
    }

    void setUseHTM(bool use) {
        useHTM.store(use);
    }
};

int main() {
    HybridTransactionalMemorySystem htms;
    htms.initializeMemory(100, 5);
    htms.initializeMemory(200, 10);

    auto incrementOperation = [](HybridTransactionalMemorySystem::Transaction& tx) {
        int val1 = tx.fetch(100);
        int val2 = tx.fetch(200);
        tx.store(100, val1 + 1);
        tx.store(200, val2 + 1);
    };

    const int numThreads = 5;
    const int numTransactionsPerThread = 1000;

    auto runBenchmark = [&](bool useHTM) {
        htms.setUseHTM(useHTM);
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&htms, &incrementOperation, numTransactionsPerThread]() {
                for (int j = 0; j < numTransactionsPerThread; ++j) {
                    while (!htms.executeTransaction(incrementOperation)) {
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        htms.executeTransaction([](HybridTransactionalMemorySystem::Transaction& tx) {
            std::cout << "Final values: " << tx.fetch(100) << ", " << tx.fetch(200) << std::endl;
        });

        return duration.count();
    };

    std::cout << "Running benchmark with HTM..." << std::endl;
    auto htmDuration = runBenchmark(true);
    std::cout << "HTM Duration: " << htmDuration << " ms" << std::endl;

    // Reset memory
    htms.initializeMemory(100, 5);
    htms.initializeMemory(200, 10);

    std::cout << "Running benchmark with STM..." << std::endl;
    auto stmDuration = runBenchmark(false);
    std::cout << "STM Duration: " << stmDuration << " ms" << std::endl;

    std::cout << "Speedup: " << static_cast<double>(stmDuration) / htmDuration << "x" << std::endl;

    return 0;
}