#pragma once

#include "definitions.h"

#include <fstream>
#include <map>

namespace BPlusTree
{
	using namespace std;

	/**
	 * @brief Abstraction over secondary storage (modeled as RAM)
	 *
	 */
	class AbsStorageAdapter
	{
		public:
		/**
		 * @brief reads one block of bytes from the address
		 *
		 * @param location the address from which to read
		 * @return bytes the bytes read, one block
		 */
		virtual bytes get(number location) = 0;

		/**
		 * @brief write one block of bytes to the address
		 *
		 * @param location the address to which to write
		 * @param data the block of bytes to write (must be of block size)
		 */
		virtual void set(number location, bytes data) = 0;

		/**
		 * @brief request an address to which it isi possible to write a block
		 *
		 * @return number the allocated address.
		 * Guaranteed to be writable and not to overalp with previously allocated
		 */
		virtual number malloc() = 0;

		/**
		 * @brief gives the "empty" address, so that the logic can check for "null pointers"
		 *
		 * @return number the "null" address, guaranteed to never be allocated
		 */
		virtual number empty() = 0;

		/**
		 * @brief Construct a new Abs Storage Adapter object
		 *
		 * @param blockSize the size of block in bytes (better be enough sized for the B+ tree)
		 */
		AbsStorageAdapter(number blockSize);
		virtual ~AbsStorageAdapter() = 0;

		/**
		 * @brief a getter for block size
		 *
		 * @return number the size of block in bytes
		 */
		number getBlockSize();

		protected:
		number blockSize;
	};

	/**
	 * @brief In-memory implementation of the storage adapter.
	 *
	 * Uses a RAM array as the underlying storage.
	 */
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

	/**
	 * @brief File system implementation of the storage adapter.
	 *
	 * Uses a binary file as the underlying storage.
	 */
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
