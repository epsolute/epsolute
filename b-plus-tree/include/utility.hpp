#pragma once

#include "definitions.h"

#include <string>

namespace BPlusTree
{
	using namespace std;

	/**
	 * @brief helper to convert string to bytes and pad (from right with zeros)
	 *
	 * @param text the string to convert
	 * @param BLOCK_SIZE the size of the resulting byte vector (will be right-padded with zeros)
	 * @return bytes the padded bytes
	 */
	bytes fromText(string text, number BLOCK_SIZE);

	/**
	 * @brief helper to convert the bytes to string (counterpart of fromText)
	 *
	 * @param data bytes produced with fromText
	 * @param BLOCK_SIZE same as in fromText
	 * @return string the original string supplied to fromText
	 */
	string toText(bytes data, number BLOCK_SIZE);

	/**
	 * @brief concatenates the vectors of bytes to a single vector of bytes
	 *
	 * @param count the number of inputs
	 * @param ... the POINTERS to the vectors
	 * @return bytes the single concatenated vector
	 */
	bytes concat(int count, ...);

	/**
	 * @brief splits the vector of bytes into subvectors defines by split points
	 *
	 * @param data the vector to split
	 * @param stops the offsets to use as split points
	 * @return vector<bytes> the resulting sub-vectors
	 */
	vector<bytes> deconstruct(bytes data, vector<int> stops);

	/**
	 * @brief concatenates numbers into one vector of bytes
	 *
	 * @param count the number of inputs
	 * @param ... the numbers to concat
	 * @return bytes the resulting vector
	 */
	bytes concatNumbers(int count, ...);

	/**
	 * @brief deconstruct the vector of byte back to numbers
	 *
	 * @param data the vector of bytes (usually created with concatNumbers)
	 * @return vector<number> the original numbers
	 */
	vector<number> deconstructNumbers(bytes data);

	/**
	 * @brief converts a number to bytes
	 *
	 * @param num the number to convert
	 * @return bytes the resulting bytes
	 */
	bytes bytesFromNumber(number num);

	/**
	 * @brief converts bytes back to number
	 *
	 * @param data the bytes to convert
	 * @return number the resulting number
	 */
	number numberFromBytes(bytes data);
}
