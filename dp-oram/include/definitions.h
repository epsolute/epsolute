#pragma once

#include "path-oram/oram.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <climits>
#include <string>
#include <vector>

#define KEYSIZE 32

// COLORS
// https://stackoverflow.com/a/9158263/1644554

#define RESET "\033[0m"
#define BLACK "\033[30m"			  /* Black */
#define RED "\033[31m"				  /* Red */
#define GREEN "\033[32m"			  /* Green */
#define YELLOW "\033[33m"			  /* Yellow */
#define BLUE "\033[34m"				  /* Blue */
#define MAGENTA "\033[35m"			  /* Magenta */
#define CYAN "\033[36m"				  /* Cyan */
#define WHITE "\033[37m"			  /* White */
#define BOLDBLACK "\033[1m\033[30m"	  /* Bold Black */
#define BOLDRED "\033[1m\033[31m"	  /* Bold Red */
#define BOLDGREEN "\033[1m\033[32m"	  /* Bold Green */
#define BOLDYELLOW "\033[1m\033[33m"  /* Bold Yellow */
#define BOLDBLUE "\033[1m\033[34m"	  /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m" /* Bold Magenta */
#define BOLDCYAN "\033[1m\033[36m"	  /* Bold Cyan */
#define BOLDWHITE "\033[1m\033[37m"	  /* Bold White */

namespace DPORAM
{
	using namespace std;

	// defines the integer type block ID
	// change (e.g. to unsigned int) if needed
	using number = unsigned long long;
	using uchar	 = unsigned char;
	using uint	 = unsigned int;
	using bytes	 = vector<uchar>;

	using ORAMSet = tuple<
		shared_ptr<PathORAM::AbsStorageAdapter>,
		shared_ptr<PathORAM::AbsPositionMapAdapter>,
		shared_ptr<PathORAM::AbsStashAdapter>,
		shared_ptr<PathORAM::ORAM>>;

	/**
	 * @brief Primitive exception class that passes along the excpetion message
	 *
	 * Can consume std::string, C-string and boost::format
	 */
	class Exception : public exception
	{
		public:
		explicit Exception(const char* message) :
			msg_(message)
		{
		}

		explicit Exception(const string& message) :
			msg_(message)
		{
		}

		explicit Exception(const boost::format& message) :
			msg_(boost::str(message))
		{
		}

		virtual ~Exception() throw() {}

		virtual const char* what() const throw()
		{
			return msg_.c_str();
		}

		protected:
		string msg_;
	};

	enum LOG_LEVEL
	{
		TRACE,
		DEBUG,
		INFO,
		WARNING,
		ERROR,
		CRITICAL
	};
	vector<string> logLevelStrings = {
		"TRACE",
		"DEBUG",
		"INFO",
		"WARNING",
		"ERROR",
		"CRITICAL"};

	vector<string> logLevelColors = {
		WHITE,
		CYAN,
		GREEN,
		YELLOW,
		RED,
		BOLDRED};

	std::istream&
	operator>>(std::istream& in, LOG_LEVEL& level)
	{
		string token;
		in >> token;
		auto index = find(logLevelStrings.begin(), logLevelStrings.end(), boost::to_upper_copy(token));
		if (index == logLevelStrings.end())
		{
			in.setstate(ios_base::failbit);
		}
		else
		{
			level = (LOG_LEVEL)distance(logLevelStrings.begin(), index);
		}

		return in;
	}

	inline LOG_LEVEL __logLevel;

	enum ORAM_BACKEND
	{
		InMemory,
		FileSystem,
		Redis,
		Aerospike
	};

	vector<string> oramBackendStrings = {
		"InMemory",
		"FileSystem",
		"Redis",
		"Aerospike"};

	std::istream&
	operator>>(std::istream& in, ORAM_BACKEND& backend)
	{
		string token;
		in >> token;

		vector<string> upper;
		upper.resize(oramBackendStrings.size());
		transform(oramBackendStrings.begin(), oramBackendStrings.end(), upper.begin(), [](string val) { return boost::to_upper_copy(val); });

		auto index = find(upper.begin(), upper.end(), boost::to_upper_copy(token));
		if (index == upper.end())
		{
			in.setstate(ios_base::failbit);
		}
		else
		{
			backend = (ORAM_BACKEND)distance(upper.begin(), index);
		}

		return in;
	}
}
