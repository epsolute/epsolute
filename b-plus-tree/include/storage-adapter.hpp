#pragma once

#include "definitions.h"

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

		AbsStorageAdapter(number blockSize);
		virtual ~AbsStorageAdapter() = 0;

		protected:
		number blockSize;
	};

	class InMemoryStorageAdapter : public AbsStorageAdapter
	{
		private:
		map<number, bytes> memory;
		number locationCounter = 0;

		void checkLocation(number location);

		public:
		InMemoryStorageAdapter(number blockSize);
		~InMemoryStorageAdapter() final;

		bytes get(number location) final;
		void set(number location, bytes data) final;
		number malloc() final;
	};
}
