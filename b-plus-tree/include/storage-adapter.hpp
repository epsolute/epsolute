#pragma once

#include "definitions.h"

#include <fstream>
#include <map>

namespace BPlusTree
{
	using namespace std;

	class AbsStorageAdapter
	{
		public:
		virtual bytes get(number location)			  = 0;
		virtual void set(number location, bytes data) = 0;
		virtual number malloc()						  = 0;

		virtual number empty() = 0;

		AbsStorageAdapter(number blockSize);
		virtual ~AbsStorageAdapter() = 0;

		number getBlockSize();

		protected:
		number blockSize;
	};

	class InMemoryStorageAdapter : public AbsStorageAdapter
	{
		private:
		map<number, bytes> memory;
		number locationCounter = ROOT;

		static inline const number EMPTY = 0;
		static inline const number ROOT	 = 1;

		void checkLocation(number location);

		public:
		InMemoryStorageAdapter(number blockSize);
		~InMemoryStorageAdapter() final;

		bytes get(number location) final;
		void set(number location, bytes data) final;
		number malloc() final;

		number empty() final;
	};

	class FileSystemStorageAdapter : public AbsStorageAdapter
	{
		private:
		fstream file;
		number locationCounter;

		static inline const number EMPTY = 0;

		void checkLocation(number location);

		public:
		FileSystemStorageAdapter(number blockSize, string filename, bool override);
		~FileSystemStorageAdapter() final;

		bytes get(number location) final;
		void set(number location, bytes data) final;
		number malloc() final;

		number empty() final;
	};
}
