#pragma once
#include "net_common.h"

namespace asr
{
	namespace net
	{
		// Message header to be sent at the start of all messages. The template allows 
		// use of an "enum class" to ensure messages are valid at compile time
		template <typename T>
		struct message_header
		{
			T id{};
			uint32_t size = 0;
		};

		template <typename T>
		struct message
		{
			message_header<T> header{};
			std::vector<uint8_t> body;

			// returns size of message body in bytes
			size_t size() const
			{
				return sizeof(message_header<T>) + body.size();
			}

			// Override for use with std::cout
			friend std::ostream& operator << (std::ostream& os, const message<T>& msg)
			{
				os << "ID: " << int(msg.header.id) << " Size: " << msg.header.size;
				return os;
			}

			// Pushes Plain Old Data (POD) types into the message buffer
			template <typename DataType>
			friend message<T>& operator << (message<T>& msg, const DataType& data)
			{
				// Check whether the datatype being pushed is trivially copyable
				static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

				// Cache size of the vector
				size_t i = msg.body.size();

				// Resize the vector by the size of the data being pushed
				msg.body.resize(msg.body.size() + sizeof(DataType));

				// Copy the data into the new vector space
				std::memcpy(msg.body.data() + i, &data, sizeof(DataType));
				
				// Recalcualte message size in header
				msg.header.size = msg.body.size();

				// Return message so it can be chained
				return msg;

				// 0 1 2 3 => size = 4
				// 0 1 2 3 4 5 6 => we want to insert into 4
			}

			template <typename DataType>
			friend message<T>& operator >> (message<T>& msg, DataType& data)
			{
				// Check whether the datatype being pushed is trivially copyable
				static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

				// Cache the location where pulled data starts
				size_t i = msg.body.size() - sizeof(DataType);

				// Copy data from the vector into the variable
				std::memcpy(&data, msg.body.data() + i, sizeof(DataType));

				// Shrink the vector to remove the read bytes
				msg.body.resize(i);

				// Recalculate message size
				msg.header.size = msg.body.size();
				
				// Return message so it can be chained
				return msg;

				// 0 1 2 3 4 5 6 => size = 7
				// 5 6 are part of DataType => size = 2
				// 7 - 2 = 5, 5 is the location where the data starts
			}
		};		

		// Forward declare connection
		template <typename T>
		class connection;

		template <typename T>
		struct owned_message
		{
			std::shared_ptr<connection<T>> remote = nullptr;
			message<T> msg;

			// Override for use with std::cout
			friend std::ostream& operator << (std::ostream& os, const owned_message<T>& msg)
			{
				os << msg.msg;
				return os;
			}
		};
	}
}