#include <iostream>
#include <filesystem>
#include <windows.h>

#include "storage/StorageEngine.h"
#include "collector/CollectorScheduler.h"
#include "collector/MetricSnapshot.h"
#include "queue/SpscQueue.h"
#include "queue/RingBuffer.h"
#include "api/ApiServer.h"

int main() {
    // TODO: will in future come from a config file or command line arg
    const std::string data_dir = "C:/ProgramData/PulseDB/data";

    // creates the directory tree if it doesn't already exist
    std::filesystem::create_directories(data_dir);

    pulsedb::StorageEngine storage(data_dir);
    // if storage fails to open, nothing works so exit immediately
    if (!storage.open()) {
        std::cerr << "Failed to open storage engine\n";
        return 1;
    }

    // lock free queue between the collector and writer thread
    SpscQueue<pulsedb::MetricSnapshot, 1024> queue;
    // last 5 minutes of snapshots kept in memory for a live API later
    RingBuffer<pulsedb::MetricSnapshot, 300> ring;

    //instantiates api server after ring is established
    pulsedb::ApiServer api(storage, ring, 7700);

    // starts the writer thread which drains from the queue
    storage.start_writer(queue);

    pulsedb::CollectorScheduler scheduler(queue, ring);

    api.set_shutdown_callback([&scheduler]() { scheduler.stop(); });

    // initializes all collectors then starts the first tick
    scheduler.start();

    // starting the api server after collecters
    api.start();

    std::cout << "PulseDB daemon running. Close window to exit.\n";
    // runs the asio event loop on this thread until stop() is called
    scheduler.run();
    std::cout << "main: scheduler.run() returned\n";

    // stopping api before the storage closes just in case
    api.stop();
    std::cout << "main: api.stop() returned\n";

    // drains the queue and flushes all open .pulse files before exiting
    storage.close();
    std::cout << "main: storage.close() returned\n";

    return 0;
}