#include "PulseDB.h"
#include "storage/PulseFileWriter.h"

using namespace std;

int main()
{
	cout << "Hello CMake." << endl;

	pulsedb::PulseFileWriter writer("test.pulse", pulsedb::MetricType::cpu_total, "cpu_total", "test_data/test.wal");
	writer.open();

	return 0;
}
