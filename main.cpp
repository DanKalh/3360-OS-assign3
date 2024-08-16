#include <iostream>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <pthread.h>
#include <semaphore.h>
#include <climits>
#include <limits>
#include <cstring>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>

using namespace std;

struct Page {
    int processID;
    int frameID;
    int pageNumber;
    int frequency;
    int accessTime;
    int kthAccessTime;
};

struct Process {
    int id;
    int size;
    bool isFinished;
    int faultCount;
    size_t minWorkset;
    size_t maxWorkset;
    vector<Page> pages;
    vector<int> historyPages;
};
struct Frame {
    int id;
    int pageNumber;
    int processID;
};

struct DPT {
    int processID;
    int pageNumber;
    int time;
};

struct DiskOperation {
    int processID;
    int diskAddress;
    int track;
    string operationType; 
    int seekTime = 0;
};



vector<Process> processes;
vector<Frame> diskFrames;
vector<DPT> dptEntries;
vector<Page> totalPages;

sem_t mutex_sub, mutex_main;
sem_t diskQueueSem;
int g_nX, g_nPageSize;
string algorithm;
vector<DiskOperation> diskQueue;
int currentHeadPosition = 0;

// Void Declarations
void initSemaphores();
void destroySemaphores();
void readFile(const string& filepath);
void runSimulation(const string& algorithm);
void accessPage(Process& process, int pageNumber, int currentTime, const string& algorithm);
void pageFaultHandler(Process& process, int pageNumber, int currentTime, const string& algorithm, int k = 1, const vector<DPT>* futureAccesses = nullptr);
void simulate();
void processPage(int idx, const string& algorithm);
void processWorkset();
void replacePage(Process& process, const string& algorithm, int currentTime, int k = 1, const vector<DPT>* futureAccesses = nullptr);
void diskDriverThread();
void processDiskOperation(const DiskOperation& op);
void processDisk(int idx);
void scheduleDiskOperations();
void processFIFO();
void processSSTF();
void processSCAN();

void readFile(const string& filepath) {
    ifstream file(filepath);
    if (!file) {
        cerr << "Unable to open file!" << endl;
        exit(1);
    }
    file >> g_nPageSize >> g_nX;
    int id, size;
    while (file >> id >> size) {
        // Initialize minWorkset to the largest possible size_t value and maxWorkset to 0
        processes.push_back({id, size, false, 0, std::numeric_limits<size_t>::max(), 0, {}, {}});
    }
}

void initSemaphores() {
    sem_init(&mutex_sub, 0, 0);
    sem_init(&mutex_main, 0, 0);
}

void destroySemaphores() {
    sem_destroy(&mutex_sub);
    sem_destroy(&mutex_main);
}




void pageFaultHandler(Process& process, int pageNumber, int currentTime, const string& algorithm, int k, const vector<DPT>* futureAccesses) {
    if (process.pages.size() >= static_cast<size_t>(process.size)) {
        replacePage(process, algorithm, currentTime, k, futureAccesses);
    }
    // Add the new page
    Page newPage = {process.id, -1, pageNumber, 1, currentTime, currentTime};
    process.pages.push_back(newPage);
    process.faultCount++;
}

void accessPage(Process& process, int pageNumber, int currentTime, const string& algorithm) {
    auto it = find_if(process.pages.begin(), process.pages.end(),
                      [pageNumber](const Page& p) { return p.pageNumber == pageNumber; });

    if (it == process.pages.end()) {
        pageFaultHandler(process, pageNumber, currentTime, algorithm);
    } else {
        it->accessTime = currentTime;
        it->frequency++;
        if (process.pages.size() < process.minWorkset) {
            process.minWorkset = process.pages.size();
        }
        if (process.pages.size() > process.maxWorkset) {
            process.maxWorkset = process.pages.size();
        }
    }
}

void runSimulation(const string& algorithm) {
    // Assuming dptEntries holds page requests for simulation
    for (auto& line : dptEntries) {
        Process& proc = processes[line.processID];
        if (!proc.isFinished) {
            accessPage(proc, line.pageNumber, line.time, algorithm);
        }
    }
}

void processDisk(int idx) {
    DPT& dpt = dptEntries[idx];
    // Find the corresponding frame
    auto frameIt = find_if(diskFrames.begin(), diskFrames.end(),
                           [&](const Frame& f) { return f.pageNumber == dpt.pageNumber && f.processID == dpt.processID; });

    if (frameIt == diskFrames.end()) {
        // No frame found, simulate disk loading
        Frame newFrame = {static_cast<int>(diskFrames.size()), dpt.pageNumber, dpt.processID};
        diskFrames.push_back(newFrame);
    }
    // Otherwise, the frame is already loaded (this is simplified)
}

void processPage(int idx, const string& algorithm) {
    DPT& dpt = dptEntries[idx];
    Process& proc = processes[dpt.processID];
    auto pageIt = find_if(proc.pages.begin(), proc.pages.end(),
                          [&](const Page& p) { return p.pageNumber == dpt.pageNumber; });

    if (pageIt == proc.pages.end()) {
        // Page not found, handle page fault
        pageFaultHandler(proc, dpt.pageNumber, dpt.time, algorithm);
    } else {
        // Update page access details
        pageIt->accessTime = dpt.time;
        pageIt->frequency++;
    }
}

void processWorkset() {
    for (auto& process : processes) {
        // Clear pages
        auto newEnd = remove_if(process.pages.begin(), process.pages.end(),
                                [&](const Page& p) { return (g_nX < p.accessTime); });
        process.pages.erase(newEnd, process.pages.end());
    }
}

void replacePage(Process& process, const string& algorithm, int currentTime, int k, const vector<DPT>* futureAccesses) {
    if (process.pages.empty()) return; // No pages to replace

    auto pageIt = process.pages.end(); // Iterator to the page to replace

    if (algorithm == "LIFO") {
        pageIt = --process.pages.end();
    } else if (algorithm == "MRU") {
        pageIt = max_element(process.pages.begin(), process.pages.end(),
                             [](const Page& a, const Page& b) { return a.accessTime > b.accessTime; });
    } else if (algorithm == "LRU-X") {
        pageIt = min_element(process.pages.begin(), process.pages.end(),
                             [k, currentTime](const Page& a, const Page& b) {
                                 return (currentTime - a.kthAccessTime) > (currentTime - b.kthAccessTime);
                             });
    } else if (algorithm == "LFU") {
        pageIt = min_element(process.pages.begin(), process.pages.end(),
                             [](const Page& a, const Page& b) { return a.frequency < b.frequency; });
    } else if (algorithm == "OPT-lookahead-X" && futureAccesses) {
        map<int, int> futureAccessMap;
        for (const DPT& dpt : *futureAccesses) {
            if (futureAccessMap.find(dpt.pageNumber) == futureAccessMap.end() || futureAccessMap[dpt.pageNumber] > dpt.time) {
                futureAccessMap[dpt.pageNumber] = dpt.time;
            }
        }
        pageIt = max_element(process.pages.begin(), process.pages.end(),
                             [&futureAccessMap, currentTime](const Page& a, const Page& b) {
                                 int aFuture = futureAccessMap.count(a.pageNumber) ? futureAccessMap[a.pageNumber] : INT_MAX;
                                 int bFuture = futureAccessMap.count(b.pageNumber) ? futureAccessMap[b.pageNumber] : INT_MAX;
                                 return aFuture > bFuture;
                             });
    } else if (algorithm == "WS") {
        // Implementing the WS
        int timeThreshold = 10; 
        pageIt = min_element(process.pages.begin(), process.pages.end(),
                             [currentTime, timeThreshold](const Page& a, const Page& b) {
                                 return (currentTime - a.accessTime > timeThreshold);
                             });
    }

    if (pageIt != process.pages.end()) {
        process.pages.erase(pageIt);
    }
}


void processDiskOperation(const DiskOperation& op) {
    sem_wait(&mutex_sub);  // Wait for access to the disk queue

    if (op.operationType == "read") {
        cout << "Reading from disk at track " << op.diskAddress << " for process " << op.processID << endl;
        // Simulate disk read delay
    } else if (op.operationType == "write") {
        cout << "Writing to disk at track " << op.diskAddress << " for process " << op.processID << endl;
        // Simulate disk write delay
    }

    sem_post(&mutex_main);  // Signal completion of disk operation
}


// Pre-bonus
void simulate(const string& algorithm) {
    for (auto& process : processes) {
        for (int time = 0; time < 100; ++time) {  // Simulate time slices
            int pageNum = rand() % 50;  // Random page access
            accessPage(process, pageNum, time, algorithm);
        }
        // Output the process
        cout << "Process ID: " << process.id << " - Page Faults: " << process.faultCount << endl;
        if (algorithm == "WS") {  // Only output workset sizes for the Working Set algorithm
            cout << "Minimum Workset Size: " << process.minWorkset << endl;
            cout << "Maximum Workset Size: " << process.maxWorkset << endl;
        }
    }
}

/*
// Resets the state of all processes
void initProcesses() {
    for (auto& process : processes) {
        process.pages.clear();            // Clearing pages might be necessary
        process.faultCount = 0;           // Reset fault count
        process.isFinished = false;       // Reset finished state
        // Any other necessary resets
    }
}

// Initializes or resets the disk driver system
void initDiskDriver() {
    // Clear the disk queue
    diskQueue.clear();

    // Reset disk head position or any other necessary states
    currentHeadPosition = 0;

    // Reinitialize disk-related semaphores if necessary
    sem_init(&diskQueueSem, 0, 0);
}

// Ensure this is the only definition of `simulate` in your code.
void simulate(const string& pageAlgorithm) {
    vector<string> diskAlgorithms = {"FIFO", "SSTF", "SCAN"};

    for (const auto& diskAlgorithm : diskAlgorithms) {
        cout << "Running simulation with Page Replacement Algorithm: " << pageAlgorithm
             << " and Disk Scheduling Algorithm: " << diskAlgorithm << endl;

        initProcesses();     // Initialize or reset processes before each run
        initDiskDriver();    // Initialize or reset the disk driver

        std::thread diskThread;
        if (diskAlgorithm == "FIFO") {
            diskThread = std::thread(processFIFO);
        } else if (diskAlgorithm == "SSTF") {
            diskThread = std::thread(processSSTF);
        } else if (diskAlgorithm == "SCAN") {
            diskThread = std::thread(processSCAN);
        }

        // Simulate page requests and manage them based on the algorithm
        for (auto& line : dptEntries) {
            Process& proc = processes[line.processID];
            if (!proc.isFinished) {
                accessPage(proc, line.pageNumber, line.time, pageAlgorithm);
            }
        }

        // Ensure the disk thread completes before moving on
        if (diskThread.joinable()) {
            diskThread.join();
        }

        // Print results after all processing is complete
        for (const auto& process : processes) {
            cout << "Process ID: " << process.id << " - Page Faults: " << process.faultCount << endl;
            if (pageAlgorithm == "WS") {
                cout << "Minimum Workset Size: " << process.minWorkset << endl;
                cout << "Maximum Workset Size: " << process.maxWorkset << endl;
            }
        }
    }
}

*/


void diskDriver() {
    while (true) {
        sem_wait(&diskQueueSem);  // Wait for an operation to be queued

        if (!diskQueue.empty()) {
            DiskOperation op = diskQueue.front();
            diskQueue.erase(diskQueue.begin()); 

            // Simulate disk operation delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate disk access time

            processDiskOperation(op);

            sem_post(&mutex_main);  // Signal that disk operation is complete
        } else {
            sem_post(&diskQueueSem);  // Prevent deadlock if queue is empty
        }
    }
}

void diskDriverThread() {
    while (true) {
        sem_wait(&diskQueueSem);  // Wait for an operation
        if (!diskQueue.empty()) {
            DiskOperation op = diskQueue.front();
            diskQueue.erase(diskQueue.begin());
            processDiskOperation(op);
        }
        sem_post(&diskQueueSem);  // Release the semaphore
    }
}


int main(int argc, char* argv[]) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " <config_file> <algorithm>\n";
        return 1;
    }

    string config_file = argv[1];
    string algorithm = argv[2];

    readFile(config_file); // Load data

    vector<string> allAlgorithms = {"LIFO", "MRU", "LRU-X", "LFU", "OPT-lookahead-X", "WS"};

    if (algorithm == "ALL") {
        for (const auto& alg : allAlgorithms) {
            cout << "Running: " << alg << endl;
            initSemaphores();
            simulate(alg);
            destroySemaphores();
        }
    } else if (find(allAlgorithms.begin(), allAlgorithms.end(), algorithm) != allAlgorithms.end()) {
        initSemaphores();
        simulate(algorithm);
        destroySemaphores();
    } else {
        cout << "Invalid algorithm. Please use one of the following: " << endl;
        for (const auto& alg : allAlgorithms) {
            cout << alg << ", ";
        }
        cout << "or ALL for all algorithms.\n";
        return 1;
    }
    
    return 0;
}



/* Bonus NOT WORKING

void scheduleDiskOperations(const string& algorithm) {
    sem_wait(&diskQueueSem);  // Synchronize access

    // Handling different scheduling algorithms
    if (algorithm == "FIFO") {
        while (!diskQueue.empty()) {
            processDiskOperation(diskQueue.front());
            diskQueue.erase(diskQueue.begin());  // Remove the processed operation
        }
    } else if (algorithm == "SSTF") {
        // Shortest Seek Time First scheduling
        if (!diskQueue.empty()) {
            auto it = min_element(diskQueue.begin(), diskQueue.end(), [&](const DiskOperation& a, const DiskOperation& b) {
                return abs(currentHeadPosition - a.diskAddress) < abs(currentHeadPosition - b.diskAddress);
            });
            processDiskOperation(*it);
            currentHeadPosition = it->diskAddress;  // Update head position
            diskQueue.erase(it);  // Remove the processed operation
        }
    } else if (algorithm == "SCAN") {
        // SCAN or elevator algorithm
        if (!diskQueue.empty()) {
            sort(diskQueue.begin(), diskQueue.end(), [&](const DiskOperation& a, const DiskOperation& b) {
                return a.diskAddress < b.diskAddress;
            });
            bool direction = true; // True means ascending
            for (auto& op : diskQueue) {
                if ((direction && op.diskAddress >= currentHeadPosition) ||
                    (!direction && op.diskAddress <= currentHeadPosition)) {
                    processDiskOperation(op);
                    currentHeadPosition = op.diskAddress; // Update head position
                }
            }
            diskQueue.clear();  // Clear all operations after processing
        }
    }

    sem_post(&diskQueueSem);
}


void processFIFO() {
    int totalSeekTime = 0;
    int operationCount = 0;
    while (!diskQueue.empty()) {
        DiskOperation op = diskQueue.front();
        int seekTime = abs(currentHeadPosition - op.diskAddress);
        totalSeekTime += seekTime;
        op.seekTime = seekTime;
        processDiskOperation(op);
        currentHeadPosition = op.diskAddress;  // Update head position after operation
        diskQueue.erase(diskQueue.begin());
        operationCount++;
    }
    cout << "Total Seek Time for FIFO: " << totalSeekTime << endl;
    cout << "Average Seek Time for FIFO: " << (operationCount ? totalSeekTime / operationCount : 0) << endl;
}



void processSSTF() {
    int totalSeekTime = 0;
    int operationCount = 0;
    while (!diskQueue.empty()) {
        auto it = std::min_element(diskQueue.begin(), diskQueue.end(), [&](const DiskOperation& a, const DiskOperation& b) {
            return abs(currentHeadPosition - a.diskAddress) < abs(currentHeadPosition - b.diskAddress);
        });
        DiskOperation op = *it;
        int seekTime = abs(currentHeadPosition - op.diskAddress);
        totalSeekTime += seekTime;
        op.seekTime = seekTime;
        processDiskOperation(op);
        currentHeadPosition = op.diskAddress;  // Update head position after operation
        diskQueue.erase(it);  // Remove the processed operation
        operationCount++;
    }
    cout << "Total Seek Time for SSTF: " << totalSeekTime << endl;
    cout << "Average Seek Time for SSTF: " << (operationCount ? totalSeekTime / operationCount : 0) << endl;
}


void processSCAN() {
    bool direction = true;  // true for moving forward/up, false for backward/down
    int totalSeekTime = 0;
    int operationCount = 0;
    while (!diskQueue.empty()) {
        // Sort elements based on disk address only if needed
        if (direction) {
            std::sort(diskQueue.begin(), diskQueue.end(), [](const DiskOperation& a, const DiskOperation& b) {
                return a.diskAddress < b.diskAddress;
            });
        } else {
            std::sort(diskQueue.begin(), diskQueue.end(), [](const DiskOperation& a, const DiskOperation& b) {
                return a.diskAddress > b.diskAddress;
            });
        }

        std::vector<DiskOperation>::iterator it = direction ?
            std::find_if(diskQueue.begin(), diskQueue.end(), [&](const DiskOperation& op) {
                return op.diskAddress >= currentHeadPosition;
            }) :
            std::find_if(diskQueue.rbegin(), diskQueue.rend(), [&](const DiskOperation& op) {
                return op.diskAddress <= currentHeadPosition;
            }).base();

        if (it != diskQueue.end() && direction) {
            while (it != diskQueue.end()) {
                DiskOperation op = *it;
                int seekTime = abs(currentHeadPosition - op.diskAddress);
                totalSeekTime += seekTime;
                op.seekTime = seekTime;
                processDiskOperation(op);
                currentHeadPosition = op.diskAddress;
                it = diskQueue.erase(it);
                operationCount++;
            }
            direction = !direction;  // Change direction
        } else if (it != diskQueue.begin() && !direction) {
            do {
                --it;
                DiskOperation op = *it;
                int seekTime = abs(currentHeadPosition - op.diskAddress);
                totalSeekTime += seekTime;
                op.seekTime = seekTime;
                processDiskOperation(op);
                currentHeadPosition = op.diskAddress;
                diskQueue.erase(it);
                operationCount++;
            } while (it != diskQueue.begin());
            direction = !direction;  // Change direction
        } else {
            direction = !direction;  // Flip direction if no suitable operation found
        }
    }
    cout << "Total Seek Time for SCAN: " << totalSeekTime << endl;
    cout << "Average Seek Time for SCAN: " << (operationCount ? totalSeekTime / operationCount : 0) << endl;
}

*/