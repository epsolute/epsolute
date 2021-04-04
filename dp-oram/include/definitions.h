#pragma once

#define USE_AEROSPIKE false

#include "path-oram/oram.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <climits>
#include <codecvt>
#include <string>
#include <vector>

#define KEYSIZE 32

// WARNING: this is supposed to be greater than the absolute value of the smallest element in the dataset
#define OFFSET 20000000

#define RPC_PORT 8787

// COLORS
// https://stackoverflow.com/a/9158263/1644554

#define RESET L"\033[0m"
#define BLACK L"\033[30m"			   /* Black */
#define RED L"\033[31m"				   /* Red */
#define GREEN L"\033[32m"			   /* Green */
#define YELLOW L"\033[33m"			   /* Yellow */
#define BLUE L"\033[34m"			   /* Blue */
#define MAGENTA L"\033[35m"			   /* Magenta */
#define CYAN L"\033[36m"			   /* Cyan */
#define WHITE L"\033[37m"			   /* White */
#define BOLDBLACK L"\033[1m\033[30m"   /* Bold Black */
#define BOLDRED L"\033[1m\033[31m"	   /* Bold Red */
#define BOLDGREEN L"\033[1m\033[32m"   /* Bold Green */
#define BOLDYELLOW L"\033[1m\033[33m"  /* Bold Yellow */
#define BOLDBLUE L"\033[1m\033[34m"	   /* Bold Blue */
#define BOLDMAGENTA L"\033[1m\033[35m" /* Bold Magenta */
#define BOLDCYAN L"\033[1m\033[36m"	   /* Bold Cyan */
#define BOLDWHITE L"\033[1m\033[37m"   /* Bold White */

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

#define COMMA ,
#define PROGRAM_OPTIONS_ENUM(ENUM_TYPE, enums, strings)                                                                                          \
	enum ENUM_TYPE                                                                                                                               \
	{                                                                                                                                            \
		enums                                                                                                                                    \
	};                                                                                                                                           \
                                                                                                                                                 \
	inline vector<wstring> ENUM_TYPE##_strings = {                                                                                               \
		strings};                                                                                                                                \
                                                                                                                                                 \
	inline std::istream&                                                                                                                         \
	operator>>(std::istream& in, ENUM_TYPE& backend)                                                                                             \
	{                                                                                                                                            \
		string token;                                                                                                                            \
		in >> token;                                                                                                                             \
                                                                                                                                                 \
		wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;                                                                                  \
		auto wtoken = converter.from_bytes(boost::to_upper_copy(token));                                                                         \
                                                                                                                                                 \
		vector<wstring> upper;                                                                                                                   \
		upper.resize(ENUM_TYPE##_strings.size());                                                                                                \
		transform(ENUM_TYPE##_strings.begin(), ENUM_TYPE##_strings.end(), upper.begin(), [](wstring val) { return boost::to_upper_copy(val); }); \
                                                                                                                                                 \
		auto index = find(upper.begin(), upper.end(), wtoken);                                                                                   \
		if (index == upper.end())                                                                                                                \
		{                                                                                                                                        \
			in.setstate(ios_base::failbit);                                                                                                      \
		}                                                                                                                                        \
		else                                                                                                                                     \
		{                                                                                                                                        \
			backend = (ENUM_TYPE)distance(upper.begin(), index);                                                                                 \
		}                                                                                                                                        \
                                                                                                                                                 \
		return in;                                                                                                                               \
	}

	PROGRAM_OPTIONS_ENUM(
		LOG_LEVEL,
		ALL COMMA TRACE COMMA DEBUG COMMA INFO COMMA WARNING COMMA ERROR COMMA CRITICAL,
		L"ALL" COMMA L"TRACE" COMMA L"DEBUG" COMMA L"INFO" COMMA L"WARNING" COMMA L"ERROR" COMMA L"CRITICAL")

	inline vector<wstring> logLevelColors = {
		BOLDWHITE,
		WHITE,
		CYAN,
		GREEN,
		YELLOW,
		RED,
		BOLDRED};

	inline LOG_LEVEL __logLevel;

	PROGRAM_OPTIONS_ENUM(
		QUERY_MULTIPLE_T,
		QFirst COMMA QSecond COMMA QMultiple,
		L"First" COMMA L"Second" COMMA L"Multiple")

	PROGRAM_OPTIONS_ENUM(
		ORAM_BACKEND,
		InMemory COMMA FileSystem COMMA Redis,
		L"InMemory" COMMA L"FileSystem" COMMA L"Redis")
}
