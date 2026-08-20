#include <iostream>
#include <filesystem>
#include <windows.h>

#include "common/Config.h"
#include "storage/StorageEngine.h"
#include "storage/RetentionManager.h"
#include "collector/CollectorScheduler.h"
#include "collector/MetricSnapshot.h"
#include "queue/SpscQueue.h"
#include "queue/RingBuffer.h"
#include "api/ApiServer.h"

int main() {
    pulsedb::Config config = pulsedb::load_config();

    std::filesystem::create_directories(config.data_directory);

    pulsedb::RetentionConfig retention{
    config.retention_raw_days,
    config.retention_1min_days,
    config.retention_1hr_days
    };

    pulsedb::StorageEngine storage(config.data_directory, retention);
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
    pulsedb::ApiServer api(storage, ring, config.api_port);

    // starts the writer thread which drains from the queue
    storage.start_writer(queue);

    pulsedb::CollectorScheduler scheduler(queue, ring, config.collection_interval_ms);

    api.set_process_collector(scheduler.get_process_collector());
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