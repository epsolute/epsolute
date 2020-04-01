#include "utility.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <cstdarg>
#include <iomanip>
#include <sstream>
#include <vector>

namespace BPlusTree
{
	using namespace std;

	bytes fromText(string text, number BLOCK_SIZE)
	{
		stringstream padded;
		padded << setw(BLOCK_SIZE) << left << text;
		text = padded.str();

		return bytes((uchar *)text.c_str(), (uchar *)text.c_str() + text.length());
	}

	string toText(bytes data, number BLOCK_SIZE)
	{
		char buffer[BLOCK_SIZE];
		memset(buffer, 0, sizeof buffer);
		copy(data.begin(), data.end(), buffer);
		buffer[BLOCK_SIZE - 1] = '\0';
		auto text			   = string(buffer);
		boost::algorithm::trim_right(text);
		return text;
	}

	bytes concat(int count, ...)
	{
		va_list args;
		va_start(args, count);

		bytes result;
		for (int i = 0; i < count; i++)
		{
			bytes *vec = va_arg(args, bytes *);
			result.insert(result.end(), vec->begin(), vec->end());
		}
		va_end(args);

		return result;
	}

	bytes concatNumbers(int count, ...)
	{
		va_list args;
		va_start(args, count);

		number numbers[count];

		bytes result;
		for (int i = 0; i < count; i++)
		{
			numbers[i] = va_arg(args, number);
		}
		va_end(args);

		return bytes((uchar *)numbers, (uchar *)numbers + count * sizeof(number));
	}

	bytes bytesFromNumber(number num)
	{
		number material[1] = {num};
		return bytes((uchar *)material, (uchar *)material + sizeof(number));
	}

	number numberFromBytes(bytes data)
	{
		uchar buffer[sizeof(number)];
		copy(data.begin(), data.end(), buffer);
		return ((number *)buffer)[0];
	}

	vector<bytes> deconstruct(bytes data, vector<int> stops)
	{
		vector<bytes> result;
		result.resize(stops.size() + 1);
		for (unsigned int i = 0; i <= stops.size(); i++)
		{
			bytes buffer(
				data.begin() + (i == 0 ? 0 : stops[i - 1]),
				data.begin() + (i == stops.size() ? data.size() : stops[i]));

			result[i] = buffer;
		}

		return result;
	}

	vector<number> deconstructNumbers(bytes data)
	{
		auto count = data.size() / sizeof(number);

		uchar buffer[count * sizeof(number)];
		copy(data.begin(), data.end(), buffer);
		return vector<number>((number *)buffer, (number *)buffer + count);
	}
}
