#include "storage-adapter.hpp"

#include <boost/format.hpp>

namespace BPlusTree
{
	using namespace std;
	using boost::format;

#pragma region AbsStorageAdapter

	AbsStorageAdapter::~AbsStorageAdapter()
	{
	}

	AbsStorageAdapter::AbsStorageAdapter(number blockSize) :
		blockSize(blockSize)
	{
	}

	number AbsStorageAdapter::getBlockSize()
	{
		return blockSize;
	}

#pragma endregion AbsStorageAdapter

#pragma region InMemoryStorageAdapter

	InMemoryStorageAdapter::InMemoryStorageAdapter(number blockSize) :
		AbsStorageAdapter(blockSize)
	{
	}

	InMemoryStorageAdapter::~InMemoryStorageAdapter()
	{
	}

	bytes InMemoryStorageAdapter::get(number location)
	{
		checkLocation(location);

		return memory[location];
	}

	void InMemoryStorageAdapter::set(number location, bytes data)
	{
		if (data.size() != blockSize)
		{
			throw Exception(boost::format("data size (%1%) does not match block size (%2%)") % data.size() % blockSize);
		}

		checkLocation(location);

		memory[location] = data;
	}

	number InMemoryStorageAdapter::malloc()
	{
		return locationCounter++;
	}

	number InMemoryStorageAdapter::empty()
	{
		return EMPTY;
	}

	void InMemoryStorageAdapter::checkLocation(number location)
	{
		if (location >= locationCounter)
		{
			throw Exception(boost::format("attempt to access memory that was not malloced (%1%)") % location);
		}
	}

#pragma endregion InMemoryStorageAdapter

#pragma region FileSystemStorageAdapter

	FileSystemStorageAdapter::FileSystemStorageAdapter(number blockSize, string filename, bool override) :
		AbsStorageAdapter(blockSize)
	{
		auto flags = fstream::in | fstream::out | fstream::binary;
		if (override)
		{
			flags |= fstream::trunc;
		}

		file.open(filename, flags);
		if (!file)
		{
			throw boost::str(boost::format("cannot open %1%: %2%") % filename % strerror(errno));
		}

		locationCounter = override ? (number)file.tellg() : blockSize;
	}

	FileSystemStorageAdapter::~FileSystemStorageAdapter()
	{
		file.close();
	}

	bytes FileSystemStorageAdapter::get(number location)
	{
		checkLocation(location);

		uchar placeholder[blockSize];
		file.seekg(location * blockSize, file.beg);
		file.read((char *)placeholder, blockSize);

		return bytes(placeholder, placeholder + blockSize);
	}

	void FileSystemStorageAdapter::set(number location, bytes data)
	{
		if (data.size() != blockSize)
		{
			throw Exception(boost::format("data size (%1%) does not match block size (%2%)") % data.size() % blockSize);
		}

		checkLocation(location);

		uchar placeholder[blockSize];
		copy(data.begin(), data.end(), placeholder);

		file.seekp(location * blockSize, file.beg);
		file.write((const char *)placeholder, blockSize);
	}

	number FileSystemStorageAdapter::malloc()
	{
		return locationCounter += blockSize;
	}

	number FileSystemStorageAdapter::empty()
	{
		return EMPTY;
	}

	void FileSystemStorageAdapter::checkLocation(number location)
	{
		if (location > locationCounter || location % blockSize != 0)
		{
			throw Exception(boost::format("attempt to access memory that was not malloced (%1%)") % location);
		}
	}

#pragma endregion FileSystemStorageAdapter

}
