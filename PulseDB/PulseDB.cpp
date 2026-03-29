#include "PulseDB.h"
#include "storage/PulseFileWriter.h"

using namespace std;

int main()
{
	cout << "Hello CMake." << endl;

	pulsedb::PulseFileWriter writer("test.pulse", MetricType::cpu_total, "cpu_total");
	writer.open();

	return 0;
}
