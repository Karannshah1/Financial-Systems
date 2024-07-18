#include <iostream>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <stdexcept>

class TransactionalMemorySystem {
private:
    struct DataCell {
        int content;
        std::atomic<unsigned> modificationCount;
        std::mutex cellLock;

        DataCell(int initial) : content(initial), modificationCount(0) {}

        // Delete copy constructor and assignment operator
        DataCell(const DataCell&) = delete;
        DataCell& operator=(const DataCell&) = delete;

        // Implement move constructor and assignment operator
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

public:
    class MemoryTransaction {
    private:
        TransactionalMemorySystem& parentSystem;
        std::map<unsigned, unsigned> readLog;
        std::map<unsigned, int> writeBuffer;

    public:
        MemoryTransaction(TransactionalMemorySystem& system) : parentSystem(system) {}

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
            unsigned currentMod = cell.modificationCount.load();
            readLog[location] = currentMod;
            return value;
        }

        void store(unsigned location, int value) {
            writeBuffer[location] = value;
        }

        bool finalizeTransaction() {
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
    };

    void initializeMemory(unsigned location, int value) {
        std::lock_guard<std::mutex> guard(globalLock);
        dataStore.emplace(std::piecewise_construct,
                          std::forward_as_tuple(location),
                          std::forward_as_tuple(value));
    }

    void executeTransaction(const std::function<void(MemoryTransaction&)>& transactionLogic) {
        for (int attempts = 0; attempts < 3; ++attempts) {
            MemoryTransaction transaction(*this);
            transactionLogic(transaction);
            if (transaction.finalizeTransaction()) {
                return;
            }
            std::this_thread::yield();  // Back off before retry
        }
        throw std::runtime_error("Transaction failed after multiple attempts");
    }
};

int main() {
    TransactionalMemorySystem tms;
    tms.initializeMemory(100, 5);
    tms.initializeMemory(200, 10);

    auto incrementOperation = [](TransactionalMemorySystem::MemoryTransaction& tx) {
        int val1 = tx.fetch(100);
        int val2 = tx.fetch(200);
        tx.store(100, val1 + 1);
        tx.store(200, val2 + 1);
    };

    std::vector<std::thread> threadPool;
    for (int i = 0; i < 5; ++i) {
        threadPool.emplace_back([&tms, &incrementOperation]() {
            for (int j = 0; j < 2; ++j) {
                tms.executeTransaction(incrementOperation);
            }
        });
    }

    for (auto& thread : threadPool) {
        thread.join();
    }

    tms.executeTransaction([](TransactionalMemorySystem::MemoryTransaction& tx) {
        std::cout << "Final values: " << tx.fetch(100) << ", " << tx.fetch(200) << std::endl;
    });

    return 0;
}