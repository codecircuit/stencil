/*****************************************************************
 * 
 * FILE         time_measurement.h
 * 
 * DESCRIPTION  This header provides a class, which can be used to
 *              perform elegant formulated wall clock program
 *              execution time measurements. It is possible to
 *              measure different times, which are accessible with
 *              keywords. Times are handled in seconds.
 *              
 * AUTHOR       Christoph Klein
 * 
 * LAST CHANGE  2015-05-23
 * 
 ****************************************************************/

#include <sys/time.h>
#include <string>
#include <map>

class TimeMeasurer {
	public:
		TimeMeasurer() : s(0),e(0) {}

		void start(std::string key) {
			struct timeval tim;
			gettimeofday(&tim, NULL);
			startTime[key] = tim.tv_sec + (tim.tv_usec / 1000000.0);
		}

		void start() {
			struct timeval tim;
			gettimeofday(&tim, NULL);
			s = tim.tv_sec + (tim.tv_usec / 1000000.0);
		}

		double stop(std::string key) {
			struct timeval tim;
			gettimeofday(&tim, NULL);
			elapsedTime[key] = (tim.tv_sec + (tim.tv_usec/1000000.0)) - startTime[key];
			return elapsedTime[key];
		}

		double stop() {
			struct timeval tim;
			gettimeofday(&tim, NULL);
			e = (tim.tv_sec + (tim.tv_usec/1000000.0)) - s;
			return e;
		}

		double get_time(std::string key) {
			return elapsedTime[key];
		}
		double get_time() {
			return e;
		}

	private:
		double s, e;
		std::map<std::string, double> startTime;
		std::map<std::string, double> elapsedTime;
};
