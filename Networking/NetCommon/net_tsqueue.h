#pragma once
#include "net_common.h"

namespace asr
{
	namespace net
	{
		template <typename T>
		class tsqueue
		{
		public:
			tsqueue() = default;
			tsqueue(const tsqueue<T>&) = delete;
			virtual ~tsqueue() { clear(); }

		public:
			// Returns and maintains item at front of the Queue
			const T& front()
			{
				std::scoped_lock lock(muxQueue);
				return deqQueue.front();
			}

			// Returns and mantains item at the back of the Queue
			const T& back()
			{
				std::scoped_lock lock(muxQueue);
				return deqQueue.back();
			}

			// Pushes an item to the front of the Queue
			void push_front(const T& item)
			{
				std::scoped_lock lock(muxQueue);
				deqQueue.emplace_front(std::move(item));

				std::unique_lock<std::mutex> ul(muxBlocking);
				cvBlocking.notify_one();
			}

			// Pushes an item to the back of the Queue
			void push_back(const T& item)
			{
				std::scoped_lock lock(muxQueue);
				deqQueue.emplace_back(std::move(item));

				std::unique_lock<std::mutex> ul(muxBlocking);
				cvBlocking.notify_one();
			}

			// Returns true if Queue is empty
			bool empty()
			{
				std::scoped_lock lock(muxQueue);
				return deqQueue.empty();
			}

			// Returns the size of the queue
			size_t count()
			{
				std::scoped_lock lock(muxQueue);
				return deqQueue.size();
			}

			// Clears queue
			void clear()
			{
				std::scoped_lock lock(muxQueue);
				deqQueue.clear();
			}

			// Removes and returns item from front of Queue
			T pop_front()
			{
				std::scoped_lock lock(muxQueue);
				auto t = std::move(deqQueue.front());
				deqQueue.pop_front();
				return t;
			}

			void wait()
			{
				while (empty())
				{
					std::unique_lock<std::mutex> ul(muxBlocking);
					cvBlocking.wait(ul);
				}
			}

		protected:
			std::mutex muxQueue;
			std::deque<T> deqQueue;

			std::condition_variable cvBlocking;
			std::mutex muxBlocking;
		};
	}
}