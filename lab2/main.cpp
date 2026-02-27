#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <limits>
#include <thread>
#include <mutex>

using namespace std;
using namespace chrono;

struct Result {
    int count;      // кількість від'ємних елементів
    int minValue;   // найменше від'ємне число
};

// Послідовний (без паралелізації)
Result sequentialSearch(const vector<int>& data) {
    Result result = {0, INT32_MAX};
    
    for (int value : data) {
        if (value < 0) {
            result.count++;
            if (value < result.minValue) {
                result.minValue = value;
            }
        }
    }
    
    return result;
}

// З використанням м'ютексів
void workerWithMutex(int start, int end, const vector<int>& data, int& globalCount, int& globalMin, mutex& mtx) {
    int localCount = 0;
    int localMin = INT32_MAX;
    
    for (int i = start; i < end; i++) {
        if (data[i] < 0) {
            localCount++;
            if (data[i] < localMin) {
                localMin = data[i];
            }
        }
    }
    
    lock_guard<mutex> lock(mtx);
    globalCount += localCount;
    if (localMin < globalMin) {
        globalMin = localMin;
    }
}

Result parallelWithMutex(const vector<int>& data, int numThreads) {
    Result result = {0, INT32_MAX};
    mutex mtx;
    vector<thread> threads;
    
    int chunkSize = data.size() / numThreads;
    
    for (int t = 0; t < numThreads; t++) {
        int start = t * chunkSize;
        int end = (t == numThreads - 1) ? data.size() : start + chunkSize;
        threads.emplace_back(workerWithMutex, start, end, cref(data), 
                             ref(result.count), ref(result.minValue), ref(mtx));
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    return result;
}

vector<int> generateData(size_t size) {
    vector<int> data(size);
    srand(static_cast<unsigned>(time(nullptr)));
    
    // Рандомні числа від -500 до 500
    for (size_t i = 0; i < size; ++i) {
        data[i] = (rand() % 1001) - 500;
    }
    
    return data;
}

void testSmallArray(const vector<int>& data) {
    if (data.size() <= 50) { 
        cout << "\n--- test ---\n";
        cout << "Elements:\n";
        for (size_t i = 0; i < data.size(); ++i) {
            cout << "data[" << i << "] = " << data[i];
            if (data[i] < 0) cout << " <-- negative";
            cout << "\n";
        }
    }
}

int main() {
    vector<size_t> testSizes = {20, 10000, 100000, 1000000, 10000000, 100000000};
    vector<int> threadCounts = {6, 12, 24, 48, 96, 192};

    for (size_t size : testSizes) {
        cout << "\n Size: " << size << "\n";
        
        vector<int> data = generateData(size);
        
        if (size <= 100) {
            testSmallArray(data);
        }
        
        auto start = high_resolution_clock::now(); 
        Result seqResult = sequentialSearch(data);
        auto end = high_resolution_clock::now(); 
        double seqTime = duration<double>(end - start).count(); 
        
        cout << "Sequential:\n";
        cout << "  Time: " << fixed << setprecision(8) << seqTime << " с\n";
        cout << "  Number of negative elements: " << seqResult.count << "\n";
        cout << "  The smallest negative number: " << seqResult.minValue << "\n";
        
        if (size <= 100) {
            int manualCount = 0;
            int manualMin = INT32_MAX;
            for (int val : data) {
                if (val < 0) {
                    manualCount++;
                    if (val < manualMin) manualMin = val;
                }
            }
            
            cout << "Manual: count = " << manualCount 
                 << ", min = " << (manualMin != INT32_MAX ? manualMin : 0) << "\n";
            cout << "Algorithm: count = " << seqResult.count 
                 << ", min = " << seqResult.minValue << "\n";
            
            if (manualCount == seqResult.count && manualMin == seqResult.minValue) {
                cout << "Good!\n";
            } else {
                cout << "Error!\n";
            }
        }

        cout << "Mutex: \n";
        for (int numThreads : threadCounts) {
            auto startMutex = high_resolution_clock::now(); 
            Result mutexResult = parallelWithMutex(data, numThreads);
            auto endMutex = high_resolution_clock::now();
            double mutexTime = duration<double>(endMutex - startMutex).count();
            
            cout << "Threads: " << numThreads 
                 << ", Time: " << fixed << setprecision(8) << mutexTime << " s\n";
            
            if (size <= 100) {
                cout << "  Count: " << mutexResult.count 
                     << ", Min: " << mutexResult.minValue;
                if (mutexResult.count == seqResult.count && mutexResult.minValue == seqResult.minValue) {
                    cout << " Good!\n";
                } else {
                    cout << " ERROR!\n";
                }
            }
        }
        cout << string(50, '-') << "\n";
    }
    
    return 0;
}