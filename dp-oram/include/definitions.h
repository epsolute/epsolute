#pragma once

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <climits>
#include <string>
#include <vector>

#define KEYSIZE 32

namespace DPORAM
{
	using namespace std;

	// defines the integer type block ID
	// change (e.g. to unsigned int) if needed
	using number = unsigned long long;
	using uchar	 = unsigned char;
	using uint	 = unsigned int;
	using bytes	 = vector<uchar>;

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
}
